# コーディングエージェント向け実装指示

## 役割

あなたは、Windows、WebRTC、Media Foundation、Cloudflare Workersに習熟したシニアソフトウェアエンジニアとして、このリポジトリに**動作するPoCを実装**してください。

計画だけを書いて終了してはいけません。調査、設計確認、コード作成、build、test、修正、手順書更新まで現在の作業環境で進めてください。

## 最終目標

ブラウザからカメラ映像とマイク音声をWebRTC送信し、Windows 11受信アプリで受信する。

Windows受信アプリは:

- Cloudflare HTTPS APIでSessionを作る
- Join URLをQRコードで表示する
- Browser SenderのOfferをHTTPS pollingで受け取る
- native libwebrtcでAnswerを返す
- remote videoをアプリ内previewする
- remote audioをユーザー選択のWindows render endpointへ出力する
- VB-CABLE使用時は`CABLE Input`へ出力する
- remote videoをWindows 11 Media Foundation Virtual Cameraとして公開する

Browser Senderは:

- QR URLから同一originのHTTPS pageを開く
- camera/microphone permissionを取得する
- Sessionをclaimする
- OffererとしてNon-Trickle ICEのOfferを作る
- HTTPS APIへOfferを送る
- AnswerをHTTPS pollingする
- WebRTC mediaをWindowsへ送る

Cloudflareは:

- Worker Static AssetsでSender pageを配信する
- REST APIを提供する
- 1 Session = 1 SQLite-backed Durable Objectで短命stateを管理する
- Sessionを5分で削除する
- 任意でCloudflare TURN短期credentialをserver-side生成する
- mediaそのものは中継・保存しない

## 最初に必ず読むファイル

次の順で読み、仕様差異があれば`specs/openapi.yaml`と本プロンプトの明示要件を優先し、`docs/`全体との整合を取ってください。

1. `README.md`
2. `docs/00_project_brief.md`
3. `docs/01_requirements_and_scope.md`
4. `docs/02_architecture.md`
5. `docs/03_session_and_signaling_protocol.md`
6. `docs/04_cloudflare_worker_design.md`
7. `docs/05_browser_sender_design.md`
8. `docs/06_windows_receiver_design.md`
9. `docs/07_virtual_camera_design.md`
10. `docs/08_vb_cable_audio_design.md`
11. `docs/09_security_and_privacy.md`
12. `docs/10_test_and_acceptance.md`
13. `docs/11_delivery_plan.md`
14. `docs/12_risks_and_decisions.md`
15. `docs/13_reference_sources.md`
16. `specs/openapi.yaml`
17. `specs/frame_pipe_protocol.md`
18. `specs/repository_layout.md`

## 作業姿勢

- 既存repositoryがある場合は、その構造・build・styleを先に調べ、必要最小限の変更で統合する。
- repositoryが空なら`specs/repository_layout.md`を基準に作成する。
- 未確認のAPI名やGN flagを推測で書かない。固定するSDK/revisionのheader、source、公式資料で確認する。
- 単にコンパイルエラーを避けるためのstubやfake実装をcritical pathに残さない。
- 小さく明示的なcodeを優先する。不要な抽象化、DI framework、巨大なutility layer、独自task frameworkを作らない。
- error handling、cleanup、timeout、cancellationをhappy pathと同時に実装する。
- 無制限queue、無制限retry、無制限logを禁止する。
- secret、SDP、Token、TURN credentialをlogしない。
- 作業中に仕様から外れる必要が出たら、勝手に方向転換せず`docs/adr/NNNN-*.md`へ理由、選択肢、影響を記録する。
- 外部secretがなくても、STUN-only開発設定、mock、fixtureで可能な範囲を完成させる。
- 管理者権限や実機操作が必要なtestだけを明確にmanualとして残し、それ以外を実行する。
- Internetへ接続できるなら、公式一次資料とupstream sourceを優先する。
- 依存softwareのlicenseを確認し、`THIRD_PARTY_NOTICES.md`へ記録する。

## 絶対に変更しない初期方針

次をCore PoCへ追加・置換しない。

- WebSocket
- Server-Sent Events
- D1
- Workers KV
- R2
- SFU/Cloudflare Calls
- 長期DB
- ユーザーアカウント
- custom Windows virtual microphone driver
- Electron
- Qt
- 録画
- 画面共有
- multi-party
- Trickle ICE
- ICE Restart
- 自動再ネゴシエーション
- 1080p/4K output
- 低遅延最適化
- H.264 only negotiation

初期版は:

- HTTPS REST + polling
- Non-Trickle ICE
- Browser Offerer
- Windows Answerer
- 720p30 NV12 Virtual Camera
- VB-CABLE等へのaudio render
- Session lifetime Virtual Camera
- one Sender / one Receiver / one Session

## 実装開始時の成果物

最初に次を作成または更新する。

### `IMPLEMENTATION_STATUS.md`

含める:

- 現在のmilestone
- 完了項目
- 未完項目
- 実行したcommands
- build/test result
- manual test待ち
- blocker
- design deviation
- pinned dependencies

### `KNOWN_ISSUES.md`

実際に確認した問題だけを書く。未確認の一般論を大量に並べない。

### Dependency lock

`windows/third_party/webrtc-lock.json`へ:

- WebRTC full commit SHA
- depot_tools revision
- Visual Studio version
- Windows SDK version
- GN args
- target
- build output
- H.264 status
- verified date

main branchをfloatingで使用しない。

## 実装順序

以下の順序で進める。各milestoneでbuild/testを通し、前段の問題を残したまま次へ大量展開しない。

---

# Milestone 0: Repositoryとtoolchain

## Cloud

- Node.js/TypeScript project
- package lock
- Wrangler
- Vitest/Cloudflare test integration
- formatter/linterは小さく保つ
- production/static build command
- staging deploy command

## Windows

- C++20 x64
- top-level build script
- CMake project
- Receiver hello window
- unit test target
- `/W4`
- Release/Debug
- Windows SDK minimumを確認
- Media Foundation/COM/D3D link設定

## libwebrtc

upstream native libwebrtcを使用する。

1. 現在のWindows toolchainを検出する。
2. そのtoolchainと互換なcommitを選ぶ。
3. current mainがVisual Studio 2026等、環境にないtoolchainを要求する場合、mainを無理に使わない。
4. 適切な固定commitを選び、理由をlock fileへ書く。
5. `scripts/build-libwebrtc.ps1`を作る。
6. checkout/depot_tools/binariesをrepositoryへcommitしない。
7. reproducibleなGN/Ninja commandを出力する。
8. VP8、Opus、H.264 decoder availabilityを確認する。
9. appからlinkするminimal smoke testを作る。

revision固有のGN arg名を必ずsourceで検証する。例示された`rtc_use_h264`等を盲目的に使用しない。

完了条件:

- clean commandでCloud tests
- clean commandでWindows smoke build
- native libwebrtc link smoke test
- dependency locks

---

# Milestone 1: Cloudflare signaling

## Worker

実装route:

- `GET /v1/health`
- `POST /v1/sessions`
- `POST /v1/sessions/{id}/claim`
- `PUT /v1/sessions/{id}/offer`
- `GET /v1/sessions/{id}/offer`
- `PUT /v1/sessions/{id}/answer`
- `GET /v1/sessions/{id}/answer`
- `DELETE /v1/sessions/{id}`

`specs/openapi.yaml`と一致させる。

## Durable Object

- `SessionDurableObject`
- SQLite-backed class migration
- one Session per object name
- strong state transition checks
- fixed `expiresAt`
- Alarm
- `deleteAll()`
- pollでTTLを延長しない
- Offer/Answer最大131072 bytes
- same-body PUT retryは冪等
- conflicting second PUTは409
- double claim拒否
- same claimNonce retryは冪等
- role tokenを分離

## Token

- Session ID: 16 random bytes以上、base64url
- Receiver/Join Token: 32 random bytes以上
- digest保存
- Sender TokenはHMACで同一claimNonceから再現可能
- `TOKEN_HMAC_SECRET`はWorker Secret
- timing leakを減らす比較
- raw Token log禁止

## Static Assets

- Worker deploymentへSender assetsを含める
- APIと同一origin
- `/send/`
- CSP/security headers
- no wildcard CORS
- no third-party CDN

## TURN

- `ENABLE_TURN=false`でSTUN-only
- true時はWorkerからCloudflare APIへ短期credential request
- long-term keyはsecret
- credential TTL約600秒
- port 53 URLをfilter
- responseをSessionへ保存し両clientへ返す
- configured=trueで生成失敗したら503
- credentialをlogしない

## Tests

`docs/10_test_and_acceptance.md`のCF-001〜CF-022を実装する。

完了条件:

- local tests
- `wrangler dev`
- staging deploy可能
- dummy Offer/Answerで全flow
- expiration
- no secret in logs

---

# Milestone 2: Browser Sender

## URL

- QR形式`/send/#v=1&s=...&j=...`
- parse/validate
- sessionStorageへ一時保存
- `history.replaceState()`でfragment削除
- claim後Join Token削除
- localStorage/cookie禁止

## UI

- Start
- Stop
- local muted preview
- state
- errors
- optional device selectors
- Session expiry
- diagnostics

frameworkは使わずVanilla TypeScript。

## Media

- Start clickから`getUserMedia`
- ideal 1280×720、30fps
- audio true
- `playsInline`
- permission failure cleanup

## Signaling/WebRTC

- getUserMedia成功後にclaim
- browser is Offerer
- add transceivers as `sendonly`
- `createOffer`
- `setLocalDescription`
- ICE gathering complete待ち
- 最終`pc.localDescription`をPUT
- Answerを1秒→2秒poll
- 60秒timeout
- `setRemoteDescription`
- state events
- Stop cleanup

## Codec

- default negotiationで動作すること
- VP8 fallback保持
- H.264 preferenceはcapability APIがある場合のoptional setting
- SDP string munging禁止
- related codecsを誤って除外しない

## Tests

- unit
- Playwright fake media
- Token/fragment
- Abort/timeout
- Offer/Answer flow
- CSP

完了条件:

- deployed HTTPS pageでmobile browserからcamera/mic permission
- OfferがAPIへ格納
- fixture Answerを処理
- cleanup

---

# Milestone 3: Windows native receiver

## UI/Application

- minimal Win32
- Session作成
- Join URL QR
- Copy link
- New Session
- state
- preview
- audio output selector
- virtual camera status
- diagnostics
- Disconnect

QR生成はlocal library。外部serviceへURLを送らない。

## Signaling Client

- HTTPS certificate validation有効
- redirect制限
- timeout
- cancellation
- response body limit
- DTO schema validation
- request ID
- no secrets in logs

## PeerConnection

- Browser Offerをpoll
- SetRemoteDescription
- CreateAnswer
- SetLocalDescription
- native gathering complete待ち
- candidate-complete current local SDPをPUT
- video/audio track受信
- state/stats
- shutdown

## Video

- callbackで重い処理をしない
- latest-only slot
- workerでrotation/scale/letterbox
- 1280×720
- I420→NV12
- preview
- no unbounded queue

## Preview

最初は簡易実装でもよいが、milestone完了までにD3D11または十分に安定したWin32 previewを用意する。

## Audio

まずAudioDeviceModuleで通常のdefault render endpointへaudioが出ることを確認する。

完了条件:

- QRからBrowser→Windows接続
- remote video preview
- remote audio
- VP8/Opus
- H.264 resultを記録
- 10分連続
- clean shutdown

---

# Milestone 4: VB-CABLE routing

- render endpoints列挙
- friendly name + stable ID
- config保存
- `CABLE Input` auto-detect
- user selection
- device missing/fallback
- device change
- no double playout

固定revisionのAudioDeviceModuleで正しく選べない場合:

1. まず原因をsource/headerで確認。
2. ADRを書く。
3. `AudioTrackSinkInterface` + WASAPI shared-mode rendererへ切り替える。
4. default ADM playoutを無効化。
5. bounded PCM buffer。
6. format/clock/error test。

VB-CABLEは同梱しない。

完了条件:

- Windows Receiver output=`CABLE Input`
- downstream app input=`CABLE Output`
- remote audioを確認
- VB-CABLEなしでもgraceful

---

# Milestone 5: Virtual Camera

## 公式sample

Microsoft `Windows-Camera/Samples/VirtualCamera`を調査し、必要な契約・event・registrationを利用する。

- sourceをコピーした場合はlicense/attribution
- sample全体をrepositoryへ持ち込まない
- Synthetic/SimpleMediaSource相当を最小化
- Media Sourceはlibwebrtc非依存

## Registration/lifecycle

- x64 Media Source DLL
- COM registration
- dev install/uninstall PowerShell
- binary directoryは一般ユーザー書込不可
- Receiverから`MFCreateVirtualCamera`
- Session lifetime
- CurrentUser
- background thread
- Start/Stop/Shutdown
- app終了で消える

## Media type

- NV12
- 1280×720
- 30/1
- progressive
- one stream
- monotonic timestamps
- rational pacing

## IPC

`specs/frame_pipe_protocol.md`を正確に実装。

- Receiver pipe server
- Media Source pipe client
- byte mode
- overlapped I/O
- exact read/write
- explicit serialization
- no raw packed struct
- explicit ACL
- one active client
- latest-only
- malformed payload拒否
- Media Foundation callbackでI/O block禁止
- Media Source reader thread
- black frame when no/stale source

Receiver:

- conversion workerがNV12をpublisherへ渡す
- writer thread
- in-flight中の中間frameはdrop
- connectionなしならframeを捨てる

Media Source:

- background read
- latest local buffer
- RequestSampleはlatestをcopy
- sample cadenceは30fps
- pipe切断時black/reconnect

## Tests

- direct Media Source test harness
- protocol tests
- Camera app
- OBS
- Chrome/Edge camera list
- start/stop/reconnect
- malformed frame
- no producer
- 20回consumer open/close

完了条件:

- Browser remote videoがCamera app/OBSのVirtual Cameraに表示
- Receiver終了でsession camera消失
- no deadlock/crash
- no unbounded queue

---

# Milestone 6: Hardening

- structured JSONL logs
- central redaction
- log rotation
- stats
- diagnostics export without secrets
- TURN
- rate limiting
- 30-minute soak
- install/uninstall documentation
- licenses
- security review
- acceptance checklist
- known issues

## Build/test commands

最終READMEに少なくとも次を明記する。

```powershell
# Cloud
cd cloud
npm ci
npm test
npm run build
npx wrangler dev
npx wrangler deploy --env staging

# libwebrtc
.\scripts\build-libwebrtc.ps1 -Configuration Release

# Windows
.\scripts\build-windows.ps1 -Configuration Release
ctest --test-dir <build-dir> -C Release --output-on-failure

# Virtual Camera development installation
.\scripts\install-vcam-dev.ps1
.\scripts\uninstall-vcam-dev.ps1

# All feasible tests
.\scripts\test-all.ps1
```

実際のpath/argumentsに合わせて更新する。

## Coding quality rules

### C++

- RAII
- smart COM pointers
- no owning raw pointers
- `std::jthread`/stop tokenまたは同等の明示停止
- checked arithmetic for sizes
- `std::span`/bounded buffers
- no exceptions across COM/C ABI boundary
- HRESULTを失わない
- UI thread affinity明示
- callback lifetime明示
- locksのorderを文書化
- callback中にnetwork/disk/pipe blockしない
- no global mutable state unless unavoidable COM factory registration
- include hygiene
- `/W4`
- ReleaseとDebug

### TypeScript

- `strict: true`
- no `any` in core protocol
- runtime validation
- AbortController
- bounded request body
- exhaustive state handling
- no secret logging
- no inline script
- no permissive CORS
- no unnecessary dependency

## Security requirements

必須:

- Token in fragment
- no Token query
- no Token/SDP log
- role-specific authorization
- one-time claim
- fixed TTL
- no poll TTL extension
- body limits
- CSP
- no-store
- TURN secret server only
- pipe validation/ACL
- binary installation path protection
- third-party license notice

## 受入基準

`checklists/ACCEPTANCE_CHECKLIST.md`を全項目確認する。

最低限:

1. Windows 11でReceiver起動。
2. QR表示。
3. mobile/desktop browserでQR URLを開く。
4. camera/mic permission。
5. WebRTC接続。
6. Windows preview。
7. VB-CABLE経由audio。
8. Virtual Camera経由video。
9. 30分連続。
10. shutdown/cleanup。
11. no secrets in logs。
12. repeatable build。

## 作業結果の報告

最終応答・最終reportには以下を含める。

- 実装したもの
- repository tree
- fixed dependency versions/commits
- build commands
- test commandsと実結果
- manual testsと結果
- 未実施manual tests
- known issues
- security considerations
- design deviations
- 次の低遅延phaseへ残した項目

「実装予定」ではなく、実際の完了/未完を明確に分ける。

## Blockerへの対応

外部要因で一部を完了できない場合も作業を止めない。

例:

- Cloudflare account/secretなし:
  - local tests、Wrangler config、mock、staging手順まで完成
  - secret未設定だけをblocker化

- VB-CABLEなし:
  - endpoint selector、speaker test、manual procedureまで完成

- admin権限なし:
  - Media Source build、unit test、install scriptまで完成
  - actual registrationだけmanual blocker

- camera consumer appなし:
  - Media Source test harnessまで完成

- native libwebrtc buildがcurrent toolchainで失敗:
  - compatible revisionを選定
  - root causeとcommandsを記録
  - floating wrapperへ逃げない

困難だから計画だけに戻らず、検証可能な最大範囲を実装する。
