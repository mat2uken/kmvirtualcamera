# 01. 要件とスコープ

## 1. 機能要件

### FR-001 セッション作成

ReceiverはCloudflare APIへHTTPSでアクセスし、短命なセッションを作成できなければならない。

作成結果には少なくとも次を含む。

- 推測困難なSession ID
- Receiver Token
- Join URL
- 有効期限
- 推奨ポーリング間隔
- Receiver用RTCConfiguration

### FR-002 QRコード表示

ReceiverはJoin URLをQRコードとして表示しなければならない。

- QRコード内のJoin TokenはURLのqueryではなくfragmentへ格納する。
- Join URL全文のコピー操作も提供する。
- QRコード生成はローカルで行い、外部QR生成サービスへURLを送らない。
- v1ではWebページ内のQRコード読み取り機能を実装しない。

### FR-003 Senderのclaim

SenderはJoin Tokenを使ってSessionを一度だけclaimできなければならない。

- claim成功時にSender TokenとSender用RTCConfigurationを返す。
- 2つ目の異なるSenderからのclaimは`409 Conflict`とする。
- 同一ブラウザから同一claim nonceで再送した場合は冪等に扱える設計とする。
- Session期限切れ時は`410 Gone`とする。

### FR-004 ブラウザメディア取得

Senderは`getUserMedia()`でカメラとマイクを取得できなければならない。

- 開始はユーザー操作から行う。
- ローカルプレビューはミュートする。
- 取得失敗理由を利用者へ表示する。
- デバイス列挙が利用可能なブラウザでは選択UIを提供してよい。
- 初期制約は720p/30fpsを理想値とし、ブラウザが選べる余地を残す。

### FR-005 WebRTC Offer/Answer

- SenderがOfferを生成する。
- ReceiverがAnswerを生成する。
- 初期版はICE gatheringが`complete`になるまで待つ。
- SDPに含まれた候補を一括交換する。
- 個別ICE Candidate APIは実装しない。
- Senderは映像・音声を`sendonly`として提示する。
- Receiverは映像・音声を受信する。

### FR-006 HTTPSポーリング

- ReceiverはOfferをHTTPS GETでポーリングする。
- SenderはAnswerをHTTPS GETでポーリングする。
- データ未準備時は`204 No Content`を返す。
- 初期間隔は1秒、15秒以降は2秒を標準とする。
- 接続試行全体は60秒で打ち切る。
- Abort/キャンセルを実装し、画面終了後にポーリングを継続しない。

### FR-007 Windows映像受信

Receiverはnative libwebrtcで映像トラックを受信し、アプリ内プレビューへ表示できなければならない。

- VP8受信を必須とする。
- H.264受信を必須目標とする。
- v1でH.264をブラウザへ強制しない。
- 受信フレームの回転情報を反映する。
- UIスレッドをフレーム処理でブロックしない。

### FR-008 Windows音声受信

Receiverは音声トラックを受信し、選択したWindows render endpointへ出力できなければならない。

- Opus受信を前提とする。
- VB-CABLEの「CABLE Input」等を列挙・選択できる。
- 未導入時は明確な警告を表示し、通常のスピーカーを選択できる。
- VB-CABLE自体は同梱しない。

### FR-009 仮想カメラ

ReceiverはWindows 11 Media Foundation Virtual Cameraを開始・停止できなければならない。

- `MFVirtualCameraLifetime_Session`
- `MFVirtualCameraAccess_CurrentUser`
- v1の公開形式は1280×720、NV12、30fps
- 仮想カメラ名は例として`WebRTC Bridge Windows Virtual Camera`
- Receiver終了時にVirtual Cameraを停止・Shutdownする
- Media Source DLL登録は開発スクリプトまたはインストーラーが行う

### FR-010 フレームIPC

ReceiverとVirtual Camera Media Sourceは別プロセスとして通信する。

v1は名前付きパイプを使用する。

- Receiverがpipe server
- Media Sourceがpipe client
- 1フレーム単位の固定ヘッダー + NV12 payload
- producer側は無制限なフレームキューを持たない
- writerが遅い場合は中間フレームを捨て、最新フレームを送る
- Media Source側はbackground readerで最新フレームを保持する
- `RequestSample`はpipe readでブロックしない

### FR-011 終了処理

次の場合に安全に終了できなければならない。

- SenderのStop操作
- ReceiverのStop操作
- ブラウザタブ終了
- PeerConnection failed/disconnected
- Session期限切れ
- Worker APIエラー
- Virtual Camera consumer終了
- VB-CABLE deviceの消失

ReceiverまたはSenderは可能ならSession DELETEを行う。ただし、DELETE失敗時もTTLで削除される。

### FR-012 診断

Receiverは次を表示またはログへ出力できる。

- Session作成時刻と有効期限
- Signaling state
- ICE connection state
- Peer connection state
- 選択されたcandidate pairの種別
- 受信映像の解像度、fps、codec
- 受信音声codec、sample rate、channels
- bytes/packets received
- packet loss、jitter、frames dropped/decode
- Virtual Camera接続状態
- Audio endpoint名
- エラーコードとrequest ID

秘密情報とSDP本文はログへ出さない。

## 2. 非機能要件

### NFR-001 対象環境

- Windows 11 x64
- 最低OS build: 22000
- 主要開発対象: 最新の安定Windows 11
- Browser: 最新安定Chrome、Edgeを優先
- Safari/iOSとChrome/Androidは互換性試験対象
- Cloudflare Workers/DOは調査基準日時点の現行APIを利用

### NFR-002 安全性

- HTTPSのみ
- セッションIDは128bit以上の乱数
- Bearer Tokenは256bit相当
- Join Tokenは1回限り
- SDP本文は最大128KiB
- API responseとpoll responseは`Cache-Control: no-store`
- SDP、Token、TURN credentialをログ出力しない
- Static pageへCSP、Permissions-Policy、Referrer-Policyを設定
- Worker secretをリポジトリへ保存しない

### NFR-003 保守性

- Cloudflare、Browser、Windows Receiver、Virtual Camera Media Sourceを分離する。
- Media Source DLLはlibwebrtcへ依存しない。
- Protocol構造体はversionを持つ。
- upstream libwebrtcリビジョンとGN argsを固定する。
- エラーを握り潰さず、利用者向けメッセージと診断ログを分ける。
- 無制限なqueue、無制限retry、無制限logを禁止する。

### NFR-004 性能

低遅延最適化は非目標だが、PoC段階から悪化要因を作らない。

- 映像frame queueは最大1フレーム相当
- pipe送信は最新フレーム優先
- UIプレビューとVirtual Cameraの処理を分離
- 720p30で30分連続動作
- CPUやメモリ使用量が時間とともに単調増加しない

### NFR-005 可観測性

- 各プロセスに構造化ログを用意
- request IDをWorkerから返す
- Windows側ログはローテーションする
- Debug buildでは詳細、Release buildでは秘密情報を除いた必要最小限
- getStatsを一定間隔で取得し、UIまたは診断ファイルへ反映

## 3. 初期版の固定値

| 値 | 初期設定 |
|---|---:|
| Session TTL | 300秒 |
| Signaling timeout | 60秒 |
| Poll interval 0–15秒 | 1000ms |
| Poll interval 15–60秒 | 2000ms |
| 最大SDPサイズ | 131072 bytes |
| Video output | 1280×720 |
| Video fps | 30/1 |
| Virtual Camera pixel format | NV12 |
| Pipe最大payload | 1,382,400 bytes |
| Audio | WebRTC/Opus、内部48kHzを基本 |
| Receiver同時Session | 1 |
| Sender数 | 1 |
| Virtual Camera instance | 1 |

## 4. 将来拡張

- Trickle ICE
- ICE Restart
- TURN優先度・transport policy UI
- H.264ハードウェアdecode
- iOS H.264 hardware sender
- 1080p60
- shared memory/D3D shared texture IPC
- audio drift/clock調整強化
- Windowsサービス化
- system lifetime Virtual Camera
- installer/code signing
- 認証ユーザー、端末管理
