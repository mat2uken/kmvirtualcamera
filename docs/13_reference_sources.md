# 13. 公式参考資料

調査基準日: 2026-08-16

実装時には必ず現行ページと固定したtoolchain/revisionのheader/sourceを再確認する。

## Cloudflare

### Workers Static Assets

- https://developers.cloudflare.com/workers/static-assets/

Worker codeとHTML/CSS/JS等のStatic Assetsを同一deploymentで配信するための公式資料。

### Workers Limits

- https://developers.cloudflare.com/workers/platform/limits/

Free planを含むrequest、CPU、size等の現行上限。PoCをproductionへ移す前に再確認する。

### Durable Objects

- https://developers.cloudflare.com/durable-objects/
- https://developers.cloudflare.com/durable-objects/platform/limits/
- https://developers.cloudflare.com/durable-objects/platform/pricing/

SQLite-backed Durable Objects、強整合storage、Free plan対応等。

### Durable Object TTL

- https://developers.cloudflare.com/durable-objects/examples/durable-object-ttl/
- https://developers.cloudflare.com/durable-objects/api/alarms/

Alarmと`deleteAll()`を使った短命Session削除。

### Testing

- https://developers.cloudflare.com/workers/testing/vitest-integration/
- https://developers.cloudflare.com/durable-objects/examples/testing-with-durable-objects/

### Realtime TURN

- https://developers.cloudflare.com/realtime/turn/
- https://developers.cloudflare.com/realtime/turn/generate-credentials/
- https://developers.cloudflare.com/realtime/turn/faq/
- https://developers.cloudflare.com/realtime/pricing/

TURN long-term keyをserver-sideに保ち、短期credentialをclientへ返す。

## WebRTC standards

### WebRTC API

- https://www.w3.org/TR/webrtc/

`RTCPeerConnection`、Offer/Answer、ICE state、codec preference、stats等。

### Media Capture

- https://www.w3.org/TR/mediacapture-streams/

`getUserMedia()`、MediaStreamTrack、constraints。

### JSEP

- https://www.rfc-editor.org/rfc/rfc8829
- https://www.rfc-editor.org/rfc/rfc9429

Offer/AnswerとICE informationをapplication signalingで交換する仕様。

### Trickle ICE

- https://www.rfc-editor.org/rfc/rfc8838

初期版では未使用だが、Non-Trickleとの設計差を理解するための資料。

### ICE transport

- https://www.rfc-editor.org/rfc/rfc8445

### WebRTC transport requirements

- https://www.rfc-editor.org/rfc/rfc8835

### WebRTC video codec interoperability

- https://www.rfc-editor.org/rfc/rfc7742

VP8/H.264相互運用要件の背景。実装時はbrowser/native buildの実際のcapabilityを検証する。

## Native libwebrtc

### Development

- https://webrtc.googlesource.com/src/+/main/docs/native-code/development/
- https://webrtc.googlesource.com/src/

### Windows/Chromium toolchain

- https://chromium.googlesource.com/chromium/src/+/main/docs/windows_build_instructions.md
- https://commondatastorage.googleapis.com/chrome-infra-docs/flat/depot_tools/docs/html/depot_tools.html

main branchのtoolchain要件は変わるため、現在のmainをそのまま採用する根拠にはしない。固定commitのDEPSとbuild filesを正とする。

## Windows Virtual Camera

### API

- https://learn.microsoft.com/en-us/windows/win32/api/mfvirtualcamera/nf-mfvirtualcamera-mfcreatevirtualcamera
- https://learn.microsoft.com/en-us/windows/win32/api/mfvirtualcamera/nn-mfvirtualcamera-imfvirtualcamera
- https://learn.microsoft.com/en-us/windows/win32/api/mfvirtualcamera/ne-mfvirtualcamera-mfvirtualcameralifetime
- https://learn.microsoft.com/en-us/windows/win32/api/mfvirtualcamera/ne-mfvirtualcamera-mfvirtualcameraaccess

### Microsoft official sample

- https://github.com/microsoft/Windows-Camera/tree/master/Samples/VirtualCamera
- https://github.com/microsoft/Windows-Camera/blob/master/Samples/VirtualCamera/README.md

Media Source DLLの登録、FrameServer/FrameServerMonitorへのload、test、debug方法を確認する。

### Media Foundation

- https://learn.microsoft.com/en-us/windows/win32/medfound/media-foundation-start-page
- https://learn.microsoft.com/en-us/windows/win32/medfound/writing-a-custom-media-source
- https://learn.microsoft.com/en-us/windows/win32/medfound/media-sources

## Windows audio / WASAPI

- https://learn.microsoft.com/en-us/windows/win32/coreaudio/wasapi
- https://learn.microsoft.com/en-us/windows/win32/coreaudio/rendering-a-stream
- https://learn.microsoft.com/en-us/samples/microsoft/windows-classic-samples/wasapi-rendering/
- https://learn.microsoft.com/en-us/windows/win32/coreaudio/device-properties

AudioDeviceModule fallbackとして自前WASAPI rendererが必要になった場合に参照する。

## QR Code

実装時に採用したlibraryの公式repositoryとlicenseを`THIRD_PARTY_NOTICES.md`へ記録する。QR生成だけに用途を限定し、外部serviceへJoin URLを送信しない。

候補例:

- https://github.com/nayuki/QR-Code-generator
- https://github.com/zxing-cpp/zxing-cpp

小ささを優先するならencode-only実装を選ぶ。

## VB-CABLE

VB-CABLEは外部製品であり、本パッケージへ同梱しない。製品サイト、license、導入手順は利用者が採用時点で確認する。Receiver実装は特定製品に強く結合せず、Windows render endpoint一般を扱う。
