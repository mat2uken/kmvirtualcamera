# 06. Windows受信アプリ設計

## 1. 技術方針

- Windows 11 x64
- C++20
- classic Win32 desktop app
- native upstream libwebrtc
- WinHTTPまたはWindows標準HTTP API
- D3D11 preview
- Media Foundation
- 小さなQR encoder library
- CMakeを主build systemとし、Media Source projectだけ必要ならVisual Studio projectを併用
- COM/handle/threadはRAII
- UI frameworkは追加しない

## 2. 画面構成

v1の1画面:

```text
┌───────────────────────────────────────────────────────┐
│ WebRTC Bridge                                         │
├───────────────────────┬───────────────────────────────┤
│ QR code              │ Remote preview                │
│                       │                               │
│ Expires: 04:59        │                               │
│ [Copy Link]           │                               │
│ [New Session]         │                               │
├───────────────────────┴───────────────────────────────┤
│ State: Waiting for sender                             │
│ ICE: new / Peer: new                                  │
│ Audio output: [CABLE Input ... ▼]                     │
│ Virtual Camera: [Start/Stop]                          │
│ [Disconnect] [Diagnostics]                            │
└───────────────────────────────────────────────────────┘
```

QRとpreviewは同時表示できる。connected後はQRを小さくするか非表示にしてよい。

## 3. Module構成

```text
windows/receiver/
├── app/
│   ├── main.cpp
│   ├── application.*
│   └── app_state.*
├── ui/
│   ├── main_window.*
│   ├── qr_view.*
│   ├── preview_view.*
│   └── diagnostics_view.*
├── signaling/
│   ├── api_client.*
│   ├── polling.*
│   ├── dto.*
│   └── session_controller.*
├── rtc/
│   ├── peer_connection_client.*
│   ├── peer_observer.*
│   ├── sdp_observer.*
│   ├── video_sink.*
│   ├── audio_device_selector.*
│   ├── stats_collector.*
│   └── webrtc_factory.*
├── media/
│   ├── frame_pipeline.*
│   ├── frame_converter.*
│   ├── latest_frame.*
│   └── pipe_frame_publisher.*
├── virtual_camera/
│   └── virtual_camera_controller.*
├── platform/
│   ├── win_http.*
│   ├── com_runtime.*
│   ├── logging.*
│   └── secure_string.*
└── config/
    └── config_loader.*
```

## 4. Application state

```text
IDLE
  → CREATING_SESSION
  → WAITING_SENDER
  → APPLYING_OFFER
  → GATHERING_ANSWER
  → CONNECTING
  → CONNECTED
  → DISCONNECTING
  → IDLE

任意
  → ERROR
```

`SessionController`がこのstate machineの唯一のowner。UI eventから直接PeerConnectionやHTTP clientを操作しない。

## 5. HTTP client

要件:

- HTTPS certificate validationを無効化しない。
- request timeout:
  - connect 5秒
  - send/receive 10秒
- pollingは1 requestずつ。前request完了前に次を開始しない。
- Abort/cancel可能。
- Authorization headerはsecure wrapperで保持。
- redirectは原則拒否、または同一HTTPS originだけ許可。
- response body上限を設ける。
- JSON parse errorを明示。
- `X-Request-ID`をログへ記録。
- WinHTTP callbackからUIを直接更新しない。

## 6. native libwebrtc統合

### 6.1 リビジョン固定

upstream libwebrtcはmain branchを毎回buildしない。

実装開始時に:

1. 開発PCのVisual Studio/Windows SDKを確認。
2. そのtoolchainでbuild可能なWebRTC commitを選ぶ。
3. commit SHAを`third_party/webrtc-lock.json`へ記録。
4. depot_tools revisionも可能なら記録。
5. GN argsを記録。
6. Release x64 buildを自動化。
7. public headers/library pathをCMakeへ渡す。
8. main追従upgradeは別作業にする。

現在のChromium mainが要求するVisual Studioが開発環境より新しい可能性がある。toolchainを無断upgradeするのではなく、互換リビジョンを固定する。

### 6.2 必要codec

- VP8 decoder
- H.264 decoder
- Opus
- RTX/NACK等のlibwebrtc標準機能

H.264 supportのGN flagや依存は固定revisionで確認する。v1のbrowser側でH.264 onlyにはしないため、VP8が確実に動けば段階的な統合テストを開始できる。

### 6.3 PeerConnectionFactory

概念構成:

```text
rtc::Thread network
rtc::Thread worker
rtc::Thread signaling
AudioDeviceModule
VideoDecoderFactory
AudioDecoderFactory
PeerConnectionFactory
```

- thread lifecycleはApplicationが所有。
- shutdown順序を逆順に固定。
- COM初期化threadを明確化。
- PeerConnection callback objectの寿命をref-countで管理。
- UI raw pointerをcallbackへ渡さない。

### 6.4 Receiverとしての設定

Offer受信後:

```text
SetRemoteDescription(offer)
CreateAnswer
SetLocalDescription(answer)
wait gathering complete
serialize current local description
PUT answer
```

Native側でも最終local descriptionへcandidateが含まれていることをtestする。

### 6.5 Tracks

`OnTrack`または対応するobserver callbackで:

- video track → `VideoSinkInterface<VideoFrame>`
- audio track → AudioDeviceModule playout
- unknown/multiple track → v1では最初の1 video + 1 audioを採用し、警告

## 7. Video pipeline

libwebrtc callbackは短時間でreturnする。

```text
OnFrame(VideoFrame)
  → LatestFrameSlotへ参照/コピー
  → conversion workerをsignal
  → return

Conversion worker
  → rotation適用
  → aspect-fit 1280×720
  → black letterbox
  → I420→NV12
  → Preview更新用surface/buffer
  → Pipe publisherへpublish
```

方針:

- unbounded queue禁止
- 同時に複数frame変換しない
- 新frameが来たら古い未処理frameを置換
- libyuv等を利用
- RGB経由変換を避ける
- v1はCPU変換でよい
- 将来D3D11 texture/DXVAへ置換できるinterfaceにする

### Rotation

`VideoFrame.rotation()`が0/90/180/270の場合に適用してからscaleする。portrait映像は16:9 output内へaspect-fitし、左右black bar。

### Timestamp

- libwebrtc frame timestampを診断用に保持
- Virtual CameraにはReceiver側の単調時刻から100ns timestampを生成
- remote RTP timestampをそのままMF presentation timeにしない

## 8. Preview

- D3D11 swap chain
- UI threadはpresentだけ
- conversion workerでBGRA stagingまたはD3D texture更新
- window resizeはaspect-fit
- device lostをhandle
- remote frameなしはblack/placeholder
- preview failureがWebRTC受信やVirtual Camera出力を停止させない

PoCを急ぐ場合、最初はGDI表示でもよいが、最終MVPではD3D11へ移す。

## 9. Audio output

基本経路:

```text
WebRTC remote audio
  → AudioDeviceModule
  → selected Windows render endpoint
  → VB-CABLE "CABLE Input"
  → VB-CABLE "CABLE Output" as microphone
```

起動時にplayout devicesを列挙し、friendly nameとdevice IDを設定UIへ出す。

- configでpreferred substringを指定可能
- exact endpoint IDが保存されていれば優先
- endpoint消失時はfallbackして警告
- endpoint変更時は安全にplayout再初期化
- 音量を勝手に変更しない
- exclusive modeは使わない
- loopback captureは使わない

固定revisionのlibwebrtc ADMでendpoint選択が安定しない場合のfallback:

```text
AudioTrackSinkInterface
  → PCM callback
  → 自前WASAPI shared-mode renderer
  → selected endpoint
```

fallbackを採用した場合はdefault ADM playoutとの二重再生を無効化し、設計差分を記録する。

## 10. QR generation

- Session create responseのJoin URLをローカルQR化。
- 外部Web APIを使用しない。
- QR error correctionはMまたはQ。
- quiet zoneを確保。
- 画面DPI scaling対応。
- full URL copy button。
- URL/tokenを通常logへ出さない。
- Debug UIでもtoken全体はマスク。

小さなC++ QR encoderをvendorする場合はLICENSEを同梱する。

## 11. Virtual Camera lifecycle

起動時または明示操作時:

1. Media Foundation startup
2. Media Source DLL登録確認
3. background threadで`MFCreateVirtualCamera`
4. `MFVirtualCameraLifetime_Session`
5. `MFVirtualCameraAccess_CurrentUser`
6. `IMFVirtualCamera::Start`
7. pipe server起動
8. UIへ状態反映

停止時:

1. pipe writer停止
2. `IMFVirtualCamera::Stop`
3. `Shutdown`
4. COM参照解放
5. MF shutdownは他component終了後

Virtual Camera作成/consent関連処理をUI threadで同期実行しない。

## 12. Configuration

`config.json`例は`specs/config.example.json`。

主項目:

- signaling base URL
- session timeout/poll override
- preferred audio endpoint ID/name
- virtual camera friendly name
- auto start virtual camera
- log level/path
- TURN policyはserver responseを基本とし、clientでsecretを持たない
- developer diagnostics

Secret Tokenはconfigへ保存しない。

## 13. Logging

推奨形式: JSON Lines。

```json
{
  "ts": "...",
  "level": "info",
  "component": "signaling",
  "event": "offer_received",
  "session": "Fh9r8D...",
  "requestId": "...",
  "durationMs": 32
}
```

Token/SDP redaction filterはlogging API中央で実装する。

## 14. Shutdown順序

```text
disable UI actions
cancel HTTP/poll
best-effort DELETE
close PeerConnection
detach video sink
stop audio playout
stop frame conversion
stop pipe publisher
stop Virtual Camera
release PeerConnectionFactory
stop libwebrtc threads
release D3D
MFShutdown
CoUninitialize
```

各stopは冪等にする。Windows session終了や例外時も同じcontrollerを通す。

## 15. Windows固有テスト

- HTTP mock serverによるstate machine
- invalid/missing API responses
- libwebrtc offer fixture
- VP8/H.264 browser negotiation
- audio device enumeration
- endpoint unplug/disable
- video rotation/scale
- pipe backpressure
- repeated start/stop 20回
- Virtual Camera consumerを開閉
- app終了時のcamera消失
- sleep/resume
- network disconnect
- 30分continuous
