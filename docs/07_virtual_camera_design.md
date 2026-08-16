# 07. Windows 11仮想カメラ設計

## 1. 採用API

Windows 11のMedia Foundation Virtual Camera APIを使用する。

- `MFCreateVirtualCamera`
- `IMFVirtualCamera`
- custom Media Foundation Media Source
- session lifetime
- current-user access

最低対象:

- Windows 11 build 22000
- Windows SDK 10.0.22000.0以上

Microsoftの公式Virtual Camera sampleを基準にする。ただし、サンプル全体を無批判にコピーせず、Synthetic `SimpleMediaSource`に相当する最小構成へ絞る。

## 2. プロセス境界

Media Source DLLはReceiver.exe内で常時動くものではない。

- Virtual Camera登録時にFrameServerMonitor側へloadされる場合がある。
- 実際の利用時にFrameServer側へloadされる。
- Receiver.exeのaddress space、global変数、C++ object pointerは共有できない。
- Media Source DLLは単独でload可能でなければならない。
- Media Source DLLの依存DLLを最小化する。

## 3. v1 media type

公開するmedia typeを1つに限定する。

| 属性 | 値 |
|---|---|
| Major type | Video |
| Subtype | NV12 |
| Frame size | 1280×720 |
| Frame rate | 30/1 |
| Pixel aspect ratio | 1/1 |
| Interlace mode | Progressive |
| Nominal range | 明示可能ならlimited/fullを設計に合わせる |
| Sample duration | 333,333または誤差補正付き100ns単位 |

複数format negotiationは将来追加。最初は固定してMedia Source state machineを簡潔にする。

## 4. コンポーネント

```text
VirtualCameraMediaSource.dll
├── ClassFactory / activation
├── WebRtcBridgeMediaSource
├── WebRtcBridgeMediaStream
├── PipeFrameReceiver
├── LatestNv12Frame
├── SampleScheduler
└── Diagnostics
```

Registrar:

```text
VirtualCameraController
  → MFCreateVirtualCamera(...)
  → Start / Stop / Shutdown
```

Installer/dev script:

- DLL配置
- CLSID/InProcServer32登録
- threading model
- uninstall時のcleanup
- 必要dependency確認

## 5. Media Source state

最低限:

```text
UNINITIALIZED
  → STOPPED
  → STARTED
  → PAUSED
  → STOPPED
  → SHUTDOWN
```

Media Foundationのevent順序、stream selection、RequestSample semanticsは公式sampleを踏襲する。

必須注意:

- `Shutdown`後のmethodは`MF_E_SHUTDOWN`
- event queueを正しくshutdown
- Start/Stop/Pauseのeventを送る
- media streamごとにwork queueを持つ
- callback内でlockを長く保持しない
- RequestSampleを拒否すべきstateを明示
- sample timestampは単調増加
- buffer length/strideを正しく設定

## 6. 名前付きパイプ

v1 pipe名:

```text
\\.\pipe\WebRtcBridge.VirtualCamera.v1
```

初期版はReceiver 1 instance、Virtual Camera 1 instanceに限定する。将来、installation IDやuser SID hashをsuffixへ追加する。

### Security descriptor

pipe serverで明示ACLを設定する。

許可候補:

- current interactive user
- LOCAL SYSTEM
- LOCAL SERVICE
- 必要ならCamera Frame Serverの実行identity

`Everyone: Full Control`は禁止。実際のFrame Server identityを実機で確認し、最小権限へ絞る。

### 接続

- Receiver.exeがpipe serverを作る。
- Media Sourceのbackground threadがclientとして接続retry。
- retryは指数backoff、上限2秒。
- Shutdown eventで即時解除。
- `ConnectNamedPipe`/`ReadFile`はoverlapped I/O。
- Media Foundation callback threadでconnect/readしない。

## 7. Pipe frame protocol

完全仕様は`specs/frame_pipe_protocol.md`。

基本:

```text
FrameHeader v1
NV12 payload
```

Headerには:

- magic
- protocol version
- header size
- sequence
- width/height
- fourcc
- stride
- payload length
- timestamp
- flags

Media Sourceは固定720p NV12以外を拒否する。payload lengthを信頼せず上限検査する。

## 8. Receiver側publisher

```text
Frame conversion worker
  → publish(frame)

PipeFramePublisher
  ├─ latest slotを置換
  ├─ writer threadをsignal
  └─ return immediately

Writer thread
  ├─ client接続待ち
  ├─ 最新sequenceを取得
  ├─ header+payload write
  ├─ write中に来た中間frameは送らない
  └─ completion後にさらに新しいframeがあれば送る
```

キュー長0〜1。送信が追いつかなければframe drop。WebRTC受信callbackをpipe I/Oでブロックしない。

## 9. Media Source側reader

```text
PipeFrameReceiver thread
  → exact header read
  → validate
  → exact payload read
  → latest frame bufferへswap
  → sequence/arrival time更新
```

double-bufferまたはimmutable shared_ptrを使い、sample producerがcopy中のbufferを上書きしない。

破損frame時:

- connectionを切断
- diagnostics counter加算
- black frame継続
- bounded backoff後に再接続

## 10. Sample生成

`RequestSample`は最新frameを使って`IMFSample`を返す。

- 最新frameが新しくなくても30fps cadenceを維持するためrepeat可。
- 一定時間frameなしならblack frame。
- 直近frameを無期限にfreezeするかblackへ切り替えるかを設定化。
- 初期値: 1秒freeze後にblack + status markerを任意表示。
- PoCではplain blackでよい。

sample:

- `MFCreateMemoryBuffer`
- payload copy
- current length設定
- `SetSampleTime`
- `SetSampleDuration`
- stream eventでsample delivery

v1は1copyを許容する。将来、custom IMFMediaBuffer/shared textureへ最適化する。

## 11. Frame pacing

30fpsの100ns durationは厳密には:

```text
10,000,000 / 30 = 333,333.333...
```

毎sampleを333,333固定にすると徐々に誤差が出る。次のいずれか:

- rational accumulatorで333,333と333,334を配分
- QPCからpresentation timeを算出してdurationを調整

推奨: rational accumulator。

```text
frameIndex * 10,000,000 / 30
```

整数除算の差分を各sample time/durationに使用する。

## 12. Registration

Media Source DLLはCOM activation可能に登録する。

開発時:

- admin PowerShell
- build outputを固定dev install pathへcopy
- CLSID key登録
- InProcServer32
- ThreadingModel=Both
- architecture x64
- uninstall scriptで削除

本番:

- MSI/MSIX suitabilityを評価
- code signing
- upgrade/rollback
- 全Virtual Camera instanceのremove
- dependency bundling
- VCRuntime方針

Media Source DLLは可能ならstatic runtimeまたは明示的redist依存にし、FrameServerで`MOD_NOT_FOUND`を避ける。`dumpbin /DEPENDENTS`をCI artifactへ残す。

## 13. Virtual Camera create

概念:

```cpp
MFCreateVirtualCamera(
    MFVirtualCameraType_SoftwareCameraSource,
    MFVirtualCameraLifetime_Session,
    MFVirtualCameraAccess_CurrentUser,
    L"WebRTC Bridge",
    L"{MEDIA-SOURCE-CLSID}",
    nullptr,
    0,
    &camera);
camera->Start(nullptr);
```

実際のsignature・引数は使用Windows SDKで確認する。

- background workerから呼ぶ。
- consent/privacy failureをUIへ説明。
- `Start`後に一般アプリから列挙可能か確認。
- 同じparametersで再openされる挙動を考慮。
- `Remove`はsession cameraの通常stopでは乱用しない。
- app終了時は`Stop`/`Shutdown`。

## 14. Debug

Media SourceはReceiver processでなくFrameServer側にloadされる。

- 登録段階: FrameServerMonitorへattach
- 利用段階: FrameServerへattach
- file loggingはreentrancy/permissionを考慮
- ETWまたは安全なring logを検討
- Debug buildだけOutputDebugStringを補助利用
- pipe diagnostics counterをReceiver UIへ返すcontrol messageは将来

公式sampleのtest harnessを参考に、Media Sourceをtest process内で直接loadするunit/integration testも用意する。

## 15. 受入試験

- Cameraアプリで列挙
- OBSで列挙
- Chrome/Edgeのcamera selectorで列挙
- Teams/Zoomで列挙（導入環境がある場合）
- sourceなしでblack
- source接続後にremote映像
- source切断後black
- consumerを20回open/close
- Receiverを20回start/stop
- Camera consumerを2つ開く
- sleep/resume
- app crash後のsession camera cleanup
- DLL uninstall後に列挙されない
