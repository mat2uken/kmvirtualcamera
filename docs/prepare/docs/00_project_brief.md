# 00. プロジェクト概要

## 1. 目的

Windows 11 PCを、ブラウザからWebRTCで送られてきた映像・音声の「受信ブリッジ」として動作させる。

最終的な利用者視点の動作は次のとおり。

1. Windows受信アプリを起動する。
2. アプリがCloudflare上に短命な接続セッションを作成する。
3. アプリが接続用URLをQRコードとして表示する。
4. 送信者がスマートフォン等でQRコードを読み取り、HTTPSの送信ページをブラウザで開く。
5. ブラウザでカメラ・マイク利用を許可し、「送信開始」を押す。
6. ブラウザとWindowsアプリがHTTPSポーリングでSDPを交換する。
7. WebRTC接続確立後、Windowsアプリが映像と音声を受信する。
8. 映像はアプリ内プレビューとWindows仮想カメラへ出力する。
9. 音声はVB-CABLE等の仮想オーディオケーブルへ出力する。
10. Teams、Zoom、OBS、ブラウザ等では、仮想カメラとVB-CABLEのcapture endpointを選択する。

## 2. PoCで検証する仮説

- WebSocketサーバーをWindows上で待ち受けなくても、HTTPS REST APIと短時間ポーリングだけでWebRTCの初回接続が成立する。
- Cloudflare WorkersとDurable Objectsだけで、PoC規模の短命なシグナリング状態を管理できる。
- ブラウザをOfferer、Windows native libwebrtcをAnswererとして相互接続できる。
- Windows 11 Media Foundation Virtual Cameraを用い、カーネルモードの仮想カメラドライバーなしで一般アプリに映像を公開できる。
- 仮想マイクドライバーを自作せず、VB-CABLEのrender endpointへ受信音声を再生することで、対応アプリからマイクとして利用できる。
- 初期PoCでは低遅延チューニングを行わずとも、後からキュー削減、ハードウェアコーデック、Trickle ICE、共有メモリ等を導入できる構造にできる。

## 3. 成果物

### 3.1 Cloudflare側

- TypeScript製Cloudflare Worker
- Worker Static Assetsで配信する送信ページ
- SQLite-backed Durable Objectによるセッション管理
- HTTPS REST API
- Non-Trickle ICE用のSDP交換
- TURN設定時の短期ICEサーバー資格情報発行
- 単体テスト・統合テスト
- Wrangler設定とデプロイスクリプト

### 3.2 ブラウザ送信側

- Vanilla TypeScript/HTML/CSS
- QR URLフラグメントの解析
- カメラ・マイク取得
- WebRTC Offer生成
- ICE gathering完了待ち
- Offer送信、Answerポーリング
- ローカルプレビュー
- 状態・エラー表示
- 接続終了処理

### 3.3 Windows受信側

- C++20 x64デスクトップアプリ
- QRコード表示
- HTTPSシグナリングクライアント
- native libwebrtc PeerConnection
- 映像・音声受信
- D3D11等によるアプリ内映像プレビュー
- VB-CABLE出力先選択
- 仮想カメラ登録・開始・停止
- ログと診断情報
- 設定ファイル

### 3.4 仮想カメラ側

- Media Foundation Media Source DLL
- `MFCreateVirtualCamera`を呼ぶRegistrar
- 名前付きパイプによるフレームIPC
- 720p30 NV12固定出力
- 最新フレーム優先
- 無入力時の黒画面またはテストパターン
- 開発用インストール・アンインストールスクリプト
- 将来のMSI化を可能にする構成

## 4. PoCの成功条件

少なくとも以下を満たすこと。

- Windows 11 build 22000以上で動作する。
- Windowsアプリが表示したQRコードから送信ページを開ける。
- Chrome/Edge系ブラウザから映像・音声を送信できる。
- Windowsアプリ内で映像プレビューできる。
- Windowsアプリが受信音声を指定したVB-CABLE render endpointへ出力できる。
- WindowsのCameraアプリまたはOBSで仮想カメラを選択し、受信映像を確認できる。
- 30分連続動作してクラッシュ、無制限なキュー増加、顕著なメモリ増加がない。
- セッション終了後にSDPと一時トークンが5分以内に削除される。
- ログにSDP、Bearer Token、TURN credentialを出力しない。

## 5. 非目標

このPoCでは次を達成目標にしない。

- Glass-to-glass遅延の最小化
- iOS専用低遅延H.264 Encoder
- 4K、60fps、HDR、10bit
- 複数送信者、SFU、会議機能
- 録画・配信・ファイル保存
- Windows仮想マイクドライバーの自作
- 永続アカウント、ユーザー管理、課金
- 接続履歴の長期保存
- WebSocket/SSEシグナリング
- ICE Restart、再ネゴシエーション、画面共有への動的切替
- 完全自動アップデート
- 本番コード署名・ストア配布

## 6. 用語

| 用語 | 本文での意味 |
|---|---|
| Receiver | Windows 11受信アプリ |
| Sender | ブラウザ送信ページ |
| Signaling | SDPと接続用情報を交換するHTTPS API |
| Session | 1回のReceiverとSenderのペアリング単位 |
| Join URL | QRコードへ格納する送信ページURL |
| Join Token | SenderがSessionを一度だけclaimするための秘密 |
| Receiver Token | Receiver専用Bearer Token |
| Sender Token | claim後にSenderへ発行するBearer Token |
| Non-Trickle ICE | 候補収集完了後、候補入りSDPを一括交換する方式 |
| Virtual Camera | Windows Media Foundationが列挙するソフトウェアカメラ |
| VB-CABLE render endpoint | 通常「CABLE Input」と表示される再生先 |
| VB-CABLE capture endpoint | 通常「CABLE Output」と表示される仮想マイク入力 |
