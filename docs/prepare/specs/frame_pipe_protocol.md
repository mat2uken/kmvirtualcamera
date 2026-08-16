# Frame Pipe Protocol v1

## 1. 目的

Windows ReceiverからVirtual Camera Media Sourceへ、固定形式NV12フレームを名前付きパイプで転送する。

v1は720p30 PoC向けであり、将来のshared memory/D3D texture transportへ差し替え可能な境界を定義する。

## 2. Transport

Pipe name:

```text
\\.\pipe\WebRtcBridge.VirtualCamera.v1
```

- Receiver: server
- Media Source: client
- byte-mode pipe
- full-duplexでもよいが、v1 payloadはserver→client
- overlapped I/O
- one active client
- partial read/writeを必ず処理
- message境界をOS pipe modeへ依存せず、header lengthで再構成

## 3. Byte order / packing

- Little-endian
- Headerは明示的にserialize/deserializeする
- C++ structをそのまま`WriteFile(sizeof(struct))`しない
- compiler packing、alignment、endiannessへ依存しない
- Header v1は64 bytes固定
- reserved bytesは0
- receiverはunknown flagsを拒否またはmaskする

## 4. Frame Header v1

| Offset | Size | Type | Field | Value |
|---:|---:|---|---|---|
| 0 | 8 | bytes | magic | ASCII `WRTCVF01` |
| 8 | 2 | u16 | version | `1` |
| 10 | 2 | u16 | headerSize | `64` |
| 12 | 4 | u32 | flags | bit field |
| 16 | 8 | u64 | sequence | monotonic, starts at 1 |
| 24 | 8 | i64 | captureTimeUs | monotonic/process-relative diagnostic |
| 32 | 4 | u32 | width | `1280` |
| 36 | 4 | u32 | height | `720` |
| 40 | 4 | u32 | fourcc | `NV12` little-endian FourCC、`0x3231564E` |
| 44 | 4 | u32 | strideY | `1280` in v1 |
| 48 | 4 | u32 | strideUV | `1280` in v1 |
| 52 | 4 | u32 | payloadBytes | `1382400` |
| 56 | 4 | u32 | headerCrc32 | optional; `0` if disabled |
| 60 | 4 | u32 | payloadCrc32 | optional; `0` if disabled |

CRCを有効にする場合:

- CRC-32/ISO-HDLCを使用する。
- `headerCrc32`計算時は`headerCrc32`と`payloadCrc32`の8 bytesを0として64-byte header全体を計算する。
- `payloadCrc32`はpayload全体を計算する。
- 初期PoCではCRCを無効にし、flagsと両CRC fieldを0としてよい。local pipeではsize/version検証を優先する。

`NV12` payload:

```text
Y plane:
  strideY * height = 1280 * 720 = 921600 bytes

Interleaved UV plane:
  strideUV * height/2 = 1280 * 360 = 460800 bytes

Total:
  1382400 bytes
```

## 5. Flags

| Bit | Name | Meaning |
|---:|---|---|
| 0 | DISCONTINUITY | source reconnect/frame jump |
| 1 | KEY_VISUAL | diagnostic only; not codec keyframe |
| 2 | CRC_PRESENT | CRC fields valid |
| 3 | SOURCE_MUTED | sender video track muted |
| 4–31 | reserved | must be zero in v1 |

## 6. Validation order

Media Source:

1. Read exactly64 bytes.
2. Validate magic.
3. Validate version/headerSize.
4. Validate reserved flags.
5. Validate width=1280、height=720。
6. Validate FourCC=NV12。
7. Validate strides。
8. Compute expected payload safely with checked arithmetic。
9. Validate payloadBytes exactly。
10. If CRC_PRESENT, validate header CRC。
11. Read exactlypayloadBytes into bounded buffer。
12. If CRC_PRESENT, validate payload CRC。
13. Publish latest frame atomically。

Any failure:

- increment protocol error counter
- close pipe
- discard partial data
- reconnect with bounded backoff
- continue producing black frames
- never allocate based solely on untrusted payloadBytes

## 7. Producer backpressure

Receiver has no multi-frame send queue.

Pseudo:

```text
publish(frame):
    lock latest
    latest = frame
    unlock
    signal writer

writer:
    while running:
        wait signal/client
        snapshot newest sequence
        write header + payload
        after completion:
            if latest sequence > sent sequence:
                continue immediately with newest
            else:
                wait
```

If frame N+1 through N+5 arrive while N is being written, next write is N+5.

## 8. Consumer storage

Media Source uses two local buffers or immutable frame objects.

```text
reader writes inactive buffer
validate complete
atomic/synchronized swap active buffer
RequestSample copies active buffer
```

`RequestSample` never waits for a pipe read. It can repeat the active buffer.

## 9. Connection and heartbeat

v1 does not need a separate heartbeat packet because continuous frames act as heartbeat.

Timeouts:

- no data for 1 second: mark stale
- no data for 2 seconds: switch output to black
- pipe disconnect: reconnect
- shutdown: CancelIoEx + close handle

Future protocol may add control messages. To avoid ambiguity, v1 pipe is frame-only.

## 10. Security

Pipe ACL:

- current interactive user
- LOCAL SERVICE
- SYSTEM
- exact Frame Server identity after validation

No `Everyone` full access.

The fixed name is a PoC limitation. Future:

```text
\\.\pipe\WebRtcBridge.VirtualCamera.v2.<installation-id>
```

and handshake with installation secret/challenge.

## 11. Tests

- serialize/deserialize golden vector
- partial reads
- partial writes
- invalid magic
- unsupported version
- header size mismatch
- integer overflow inputs
- wrong dimensions
- wrong FourCC
- wrong payload length
- CRC mismatch
- disconnect halfway
- reconnect
- producer faster than consumer
- consumer faster than producer
- shutdown while I/O pending
