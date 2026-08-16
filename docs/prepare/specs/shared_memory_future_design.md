# 将来の共有メモリ/D3D IPC設計

この文書は初期PoCの実装対象ではない。名前付きパイプが性能上のbottleneckになったときの移行方針を示す。

## 1. 目的

名前付きパイプの約41.5MB/s payload copyとkernel I/Oを削減し、Virtual Camera Media Sourceが最新NV12フレームへ直接アクセスできるようにする。

## 2. 移行原則

- `IFramePublisher` / `IFrameConsumer` interfaceは維持。
- Pipeはcontrol、version negotiation、heartbeatへ残してよい。
- payload transportだけをshared memoryへ置換。
- v1 media typeは720p30 NV12のまま先に比較。
- correctness、ACL、process lifecycleを性能より優先。
- low-latency phaseで計測後に採用。

## 3. 候補A: File-backed shared mapping

ReceiverとMedia Sourceが同じ固定fileをopen/mmapする。

利点:

- Global named section privilege問題を避けやすい。
- path/ACLをinstallerで管理可能。
- crash後のdiagnosticsが可能。

課題:

- FrameServer identityからpathへaccess可能にする。
- multi-user/session。
- stale file cleanup。
- local attacker injection。
- disk-backed fileだが実際はmemory cache。

## 4. 候補B: Global named section

FrameServer側またはinstaller-managed brokerが`Global\...` sectionを作り、Receiverがopenする。

利点:

- direct shared memory。
- cross-session。

課題:

- Global object作成privilege。
- ACL。
- lifecycle owner。
- Receiver単独で作れない可能性。

## 5. 候補C: D3D11 shared texture

ReceiverがNV12/D3D textureをshared handleとして公開し、Media SourceがD3D device manager経由で利用。

利点:

- GPU path。
- CPU copy削減。
- 将来hardware decodeとの接続。

課題:

- cross-process adapter/device compatibility。
- keyed mutex/fence。
- FrameServer D3D manager。
- texture format negotiation。
- device lost。
- security handle transfer。
- Media Foundation buffer integration。

## 6. Double-buffer header concept

```text
Header
  magic/version
  width/height/fourcc/stride
  slotCount=2
  slotBytes
  atomic publishedSlot
  atomic publishedSequence
  producerHeartbeat
  slot metadata[2]

Slot 0 payload
Slot 1 payload
```

Producer:

1. active以外のslotへwrite。
2. metadata更新。
3. release memory barrier。
4. publishedSlot/sequenceをatomic publish。
5. event/control pipeでsignal。

Consumer:

1. acquire sequence/slot。
2. payload copy/reference。
3. sequence再確認。
4. 変化していればretry。
5. stale heartbeatならblack。

## 7. 採用判定

名前付きパイプbaselineと比較:

- end-to-end frame availability latency
- Receiver CPU
- FrameServer CPU
- memory bandwidth
- dropped frames
- 30/60fps scalability
- stability
- installer/admin complexity
- security

720p30でpipeが十分なら、PoC段階では移行しない。
