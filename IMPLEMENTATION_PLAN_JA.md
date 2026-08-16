# WebRTC Windows 11 仮想カメラ PoC 実装計画書 (Hono + VanJS + Open Props + libdatachannel)

## 1. 目的と概要 (Goal Description)

本プロジェクトの目的は、ブラウザからWebRTCで送信された映像と音声をWindows 11受信アプリで受信し、**Windows 11 Media Foundation Virtual Camera（仮想カメラ）**および**VB-CABLE（仮想マイク）**へルーティングして外部アプリ（Cameraアプリ、OBS、Teams、Zoom等）から利用できるPoC（Proof of Concept）を構築することです。

### 主要技術スタック
- **Cloudflare Worker**: 超高速・型安全な **`Hono`** + **`SQLite-backed Durable Objects`**
- **ブラウザ送信アプリ (`/send/`)**: **`VanJS`**（0.9kB 極小リアクティブUI）+ **`Open Props`**（モダンCSSトークン）
- **Windows 受信アプリ**: C++20 Win32 + **`libdatachannel`** + **`mbedTLS`** + **`Direct3D 11`** + **`WinHTTP`**
- **Windows 仮想カメラ**: **`Media Foundation Virtual Camera API`** (`VirtualCameraMediaSource.dll` / 依存ゼロ)
- **音声ルーティング**: **`libopus`** + **`WASAPI`** → `CABLE Input`

### 全体アーキテクチャ図

```mermaid
flowchart TD
    subgraph Browser_Sender ["ブラウザ送信側 (/send/ - VanJS + Open Props)"]
        BS_UI[VanJS UI + Open Props] --> BS_Media[getUserMedia 720p30]
        BS_Media --> BS_PC[標準 RTCPeerConnection]
        BS_PC --> BS_Offer[Offer生成 + Non-Trickle ICE収集完了]
    end

    subgraph Cloudflare ["Cloudflare Worker (Hono + Durable Object)"]
        CF_Hono[Hono Router & REST API & Static Assets] --> CF_DO[SessionDurableObject SQLite]
    end

    subgraph Windows_Receiver ["Windows 11 受信アプリ (C++20 x64 / 最小依存)"]
        WR_UI[Win32 UI + Nayuki QR + D3D11プレビュー]
        WR_Signaling[WinHTTP シグナリングクライアント]
        WR_RTC[libdatachannel + mbedTLS PeerConnection]
        WR_VideoPipeline[H.264/VP8 デパケタイズ・デコード + NV12 720p30変換]
        WR_AudioPipeline[Opus デパケタイズ・デコード + WASAPI CABLE Inputへ]
        WR_PipeServer[名前付きパイプサーバー \\.\pipe\WebRtcBridge.VirtualCamera.v1]
        WR_Registrar[MFCreateVirtualCamera セッション管理]
    end

    subgraph Media_Foundation ["Windows 11 Frame Server / Media Foundation"]
        MF_DLL[VirtualCameraMediaSource.dll (依存ゼロ)]
        MF_Stream[IMFMediaStream 720p30 NV12]
        MF_Apps[Windowsアプリ: Camera, OBS, Teams]
    end

    subgraph Audio_System ["Windows Audio Engine"]
        VB_Input[CABLE Input 再生デバイス]
        VB_Output[CABLE Output 仮想マイク入力]
        Mic_Apps[各種アプリのマイク入力]
    end

    BS_Offer -- "HTTPS PUT /v1/sessions/:id/offer" --> CF_Hono
    WR_Signaling -- "HTTPS GET /v1/sessions/:id/offer" --> CF_Hono
    WR_Signaling -- "HTTPS PUT /v1/sessions/:id/answer" --> CF_Hono
    BS_UI -- "HTTPS GET /v1/sessions/:id/answer" --> CF_Hono

    BS_PC == "WebRTC P2P (UDP/DTLS-SRTP / 任意TURN)" ==> WR_RTC
    WR_RTC --> WR_VideoPipeline
    WR_RTC --> WR_AudioPipeline

    WR_VideoPipeline --> WR_UI
    WR_VideoPipeline --> WR_PipeServer
    WR_PipeServer -- "NV12 Frame Pipe Protocol v1" --> MF_DLL
    MF_DLL --> MF_Stream --> MF_Apps

    WR_AudioPipeline --> VB_Input --> VB_Output --> Mic_Apps
```

---

## 2. 設計判断と厳守事項 (Key Architecture Invariants)

> [!IMPORTANT]
> **設計原則とアーキテクチャ要件**
> 1. **Cloudflare & Web UI**:
>    - API層: **Hono** による型安全なルーティング、エラーハンドリング、リクエストID・セキュリティヘッダーミドルウェア。
>    - Web層: **VanJS**（0.9kBの極小リアクティブDOM）+ **Open Props**（純CSSデザイントークン）による超軽量・高速・モダンUI。重量級フレームワークを排除。
> 2. **Windows & WebRTC**:
>    - **`libdatachannel`** + **`mbedTLS`** の軽量静的リンク。`libwebrtc` は使用しない。
>    - WindowsネイティブAPI（WinHTTP, D3D11, WASAPI, Media Foundation）を最大活用し、不要な巨大ライブラリ依存を徹底排除。
> 3. **シグナリング方式**: HTTPS REST API + 短時間ポーリング（初期間隔1秒→15秒以降2秒、最大60秒）。WebSocket/SSEは使用しない。
> 4. **ICE交換**: Non-Trickle ICE。ブラウザがOffer生成（Offerer）、WindowsがAnswer生成（Answerer）。双方ともICE candidate収集完了（`complete`）後の完全なSDPを一括交換する。
> 5. **セッション寿命**: 作成から5分（300秒）固定。ポーリングで延長しない。1 Session = 1 SQLite-backed Durable Object。
> 6. **トークンとセキュリティ**:
>    - QRコードのJoin TokenはURLフラグメント（`#v=1&s=...&j=...`）に格納。
>    - ページ読み込み直後に`sessionStorage`へ退避し、`history.replaceState()`でURLバーから即座に消去。
>    - トークン、SDP本文、TURN資格情報はログへ一切出力しない。
> 7. **仮想カメラ境界の完全分離**:
>    - `VirtualCameraMediaSource.dll`はWindows Frame Serverプロセスにロードされるため、外部依存ゼロ（Windows SDK標準ヘッダー・ランタイムのみ）。
>    - 受信アプリ（Receiver.exe）とMedia Source DLL間は、オーバーラップI/Oによる名前付きパイプ（`\\.\pipe\WebRtcBridge.VirtualCamera.v1`）で720p30 NV12フレームを転送（`specs/frame_pipe_protocol.md`準拠）。

---

## 3. マイルストーン別詳細実装計画 (Milestones)

### Milestone 0: リポジトリ構成・libdatachannel / Hono / VanJS + Open Props 環境構築

1. **リポジトリルート配置**:
   - 仕様書・定義ファイルをルートの`docs/`, `specs/`, `checklists/`へ整理配置。
   - ルートに`README.md`, `SECURITY.md`, `THIRD_PARTY_NOTICES.md`, `LICENSE`を作成。
   - `docs/adr/0001-use-libdatachannel-and-mbedtls.md`, `docs/adr/0002-use-hono-and-base-ui.md`, `docs/adr/0003-use-vanjs-and-open-props.md` を作成。
   - `IMPLEMENTATION_STATUS.md`, `KNOWN_ISSUES.md` を作成。
2. **Cloudflare & Web環境整備**:
   - `cloud/`: Hono, Vitest, TypeScript, Wrangler 設定。
   - `cloud/web/`: VanJS (`vanjs-core`) + Open Props (`open-props`) + TypeScript + Vite ビルド環境整備（Worker Static Assets 出力先 `./dist/public`）。
3. **Windows CMake & libdatachannel + mbedTLS ツールチェーン統合**:
   - `windows/CMakeLists.txt`にて、`libdatachannel`（C++17/20, `USE_MBEDTLS=ON`, `NO_WEBSOCKET=ON`, `NO_MEDIA=OFF`）を統合。
   - Windows MSVC `/W4`, C++20, Release/Debugビルドターゲットを設定。

---

### Milestone 1: Cloudflare シグナリング (Hono + SQLite Durable Object)

`specs/openapi.yaml`に準拠したAPIおよびセッション管理をHonoで実装：
- **Hono ルート実装 (`cloud/src/index.ts`, `cloud/src/routes/sessions.ts`)**:
  - `GET /v1/health`: ヘルスチェック
  - `POST /v1/sessions`: セッション作成（16バイト乱数Session ID、32バイトReceiver/Join Token生成、Join URL作成、5分固定TTL）
  - `POST /v1/sessions/:sessionId/claim`: Senderクレーム（Join Token検証、`claimNonce`冪等性保証、Sender Token発行）
  - `PUT /v1/sessions/:sessionId/offer`: SenderのOffer登録（128KB上限、`OFFER_READY`へ遷移）
  - `GET /v1/sessions/:sessionId/offer`: ReceiverのOffer取得（未準備時204 + `Retry-After: 1`）
  - `PUT /v1/sessions/:sessionId/answer`: ReceiverのAnswer登録（`ANSWER_READY`へ遷移）
  - `GET /v1/sessions/:sessionId/answer`: SenderのAnswer取得
  - `DELETE /v1/sessions/:sessionId`: セッション明示削除
- **SessionDurableObject (`cloud/src/durable/session-object.ts`)**:
  - SQLiteストレージへの短命データ保存。
  - 作成時のAlarm設定による5分後自動`deleteAll()`（ポーリングで延長不可）。
  - ロール別トークンハッシュ認証（定数時間比較）。
  - 同一ボディPUTの再試行に対する冪等成功とコンフリクト検出。
- **Hono ミドルウェア**:
  - `Cache-Control: no-store, max-age=0`, `Referrer-Policy: no-referrer`, `X-Request-ID`。
  - 静的アセット向け厳格なCSPヘッダー。
  - ログ出力における秘密情報マスキング。
- **自動テスト (`cloud/test/`)**:
  - `docs/10_test_and_acceptance.md`の**CF-001〜CF-022**を全件実装・検証。

---

### Milestone 2: ブラウザ送信アプリ (VanJS + Open Props)

`cloud/web/`にVanJSとOpen Propsを活用した超軽量・高レスポンス送信ページを実装：
- **UIとコンポーネント (`cloud/web/src/app.ts`, `cloud/web/src/styles.css`)**:
  - **VanJS** によるリアクティブな状態管理（セッション状態、カメラ/マイクデバイス選択、プレビュー表示、開始/停止ボタン、診断ドロワー）。
  - **Open Props** によるデザイントークン適用（洗練されたダーク/ライトテーマ、なめらかなイージング、カードシャドウ、ボタントランジション）。
  - 起動時にURLフラグメント`#v=1&s=...&j=...`をパース後、`sessionStorage`へ退避し、`history.replaceState`でURLバーから即座に削除。
- **メディア取得とWebRTC接続 (`cloud/web/src/rtc.ts`, `cloud/web/src/api.ts`)**:
  - ユーザーの「開始」クリックを起点に`getUserMedia({ video: { width: 1280, height: 720, frameRate: 30 }, audio: true })`を実行。
  - メディア取得成功後にSessionをclaim。
  - トランシーバーを`direction: "sendonly"`として追加。
  - `createOffer` → `setLocalDescription` → ICE candidate収集完了待ち（タイムアウト15秒）。
  - 収集完了後の最終`localDescription.sdp`をPUT。
  - Answerをポーリング（1秒→2秒バックオフ、全体60秒タイムアウト）。
  - Answer受信後に`setRemoteDescription`。
  - 接続状態・統計情報（`getStats`）の定期取得と停止時のクリーンアップ。

---

### Milestone 3: Windows ネイティブ受信アプリ (libdatachannel + Win32/D3D11)

`windows/receiver/`に最小依存のC++20 Win32デスクトップアプリを実装：
- **Win32 UI & プレビュー (`windows/receiver/ui/`)**:
  - メインウィンドウ、Nayuki QRコードライブラリによるローカルQR描画、D3D11スワップチェーンによるNV12/BGRAリアルタイムプレビュー。
  - 音声出力先ドロップダウン、仮想カメラ状態トグル、診断情報表示。
- **シグナリングクライアント (`windows/receiver/signaling/`)**:
  - WinHTTPを用いたHTTPSクライアント（サーバー証明書検証、タイムアウト、リクエストID追跡、JSON DTOパース）。
  - バックグラウンドワーカーによるOfferポーリング。
- **libdatachannel PeerConnectionクライアント (`windows/receiver/rtc/`)**:
  - `rtc::PeerConnection`（mbedTLSバックエンド）の設定とICE/DTLS管理。
  - Offer受信 → `setRemoteDescription` → `setLocalDescription` → 候補収集完了待ち → Answer PUT。
  - `rtc::Track`（Video / Audio）の受信ハンドラー。
- **映像パイプライン (`windows/receiver/media/`)**:
  - RTPデパケタイズ（H.264/VP8）およびデコード。
  - 最新フレームのみを保持するスレッドセーフなスロット（バッファキュー膨張の防止と最新優先ドロップ）。
  - 1280x720 30fps NV12への変換、アスペクト比維持レターボックス。
  - 変換済みNV12フレームを名前付きパイプサーバーおよびD3D11プレビューへ同時配信。

---

### Milestone 4: VB-CABLE 音声ルーティング (WASAPI)

`windows/receiver/audio/`に最小依存の音声出力処理を実装：
- **オーディオエンドポイント列挙**:
  - Windows Core Audio / WASAPI（`IMMDeviceEnumerator`）による再生デバイス列挙。
  - フレンドリー名から`CABLE Input (VB-Audio Virtual Cable)`を自動検出し優先選択。
  - 選択結果を`config.json`へ保存。
- **音声デコードとWASAPI再生**:
  - RTP Opusパケットを`libopus`でPCM (48kHz stereo/mono) にデコード。
  - WASAPI shared-mode render clientで`CABLE Input`へ直接バッファリング再生。
  - 二重再生によるハウリング・エコーの防止。

---

### Milestone 5: Windows 11 仮想カメラ (Media Foundation DLL / 依存ゼロ)

`windows/virtual-camera/`に完全独立なMedia Foundation Virtual Cameraを実装：
- **Media Source DLL (`VirtualCameraMediaSource.dll`)**:
  - COMクラスファクトリ（`InProcServer32`, `ThreadingModel=Both`）。
  - `IMFMediaSource`, `IMFMediaStream`実装（1280x720 30fps NV12単一メディアタイプ）。
  - バックグラウンドスレッドによる`\\.\pipe\WebRtcBridge.VirtualCamera.v1`へのオーバーラップ名前付きパイプクライアント接続。
  - `specs/frame_pipe_protocol.md`に準拠した厳格な64バイト`FrameHeader`検証（マジック`WRTCVF01`, version 1, 1280x720, NV12, stride 1280, 1382400バイトペイロード）。
  - レーショナルアキュムレータ（`frameIndex * 10000000 / 30`）による高精度30fpsフレームペーシング。
  - パイプ切断時またはデータ途絶時（>2秒）の自動黒画面（NV12 Black Frame）出力。
- **Registrar / ライフサイクル管理**:
  - 受信アプリ起動/要求時に`MFCreateVirtualCamera(MFVirtualCameraType_SoftwareCameraSource, MFVirtualCameraLifetime_Session, MFVirtualCameraAccess_CurrentUser, ...)`で登録・開始。
  - 受信アプリ終了時に`Stop`・`Shutdown`により自動消滅。
- **開発用インストールスクリプト**:
  - `scripts/install-vcam-dev.ps1`: DLL配置とCOM CLSIDのレジストリ登録。
  - `scripts/uninstall-vcam-dev.ps1`: レジストリ登録解除。
- **テストハーネス (`windows/virtual-camera/tests/`)**:
  - DLLをインプロセスロードし、サンプル生成、タイムスタンプ単調増加、黒画面出力、パイプ再接続を検証する自動テスト。

---

### Milestone 6: 堅牢化・統合テスト・受入確認

1. **ログと診断の堅牢化**:
   - 構造化JSON Linesログ、トークン/SDP/TURN資格情報の自動マスキングフィルター。
   - 30分間連続動作試験（メモリリーク・キュー肥大化・ハンドルリークの不在確認）。
2. **受入チェックリスト (`checklists/ACCEPTANCE_CHECKLIST.md`)**:
   - 全項目の実機検証と`IMPLEMENTATION_STATUS.md`への結果記録。
3. **ビルド・テスト手順書検証**:
   - スクリプト群（`bootstrap-cloud.ps1`, `build-windows.ps1`, `test-all.ps1`）の再現性確認。

---

## 4. 検証計画 (Verification Plan)

### 自動テスト (Automated Tests)
1. **Cloudflare シグナリングテスト (Hono + DO)**:
   ```powershell
   cd cloud
   npm test
   ```
   *期待結果*: CF-001〜CF-022の全22テストがパス。
2. **Windows ネイティブ単体テスト**:
   ```powershell
   .\scripts\build-windows.ps1 -Configuration Release
   ctest --test-dir windows/build -C Release --output-on-failure
   ```
   *期待結果*: libdatachannel PeerConnection初期化、パイププロトコル検証、NV12変換、WASAPIオーディオデバイス検索、Media Sourceテストハーネスがパス。

### 手動 E2E 検証フロー
1. **シグナリングサーバー起動**: `cloud/`にて `npx wrangler dev` を実行。
2. **仮想カメラ登録**: `.\scripts\install-vcam-dev.ps1` を実行。
3. **Windows 受信アプリ起動**: `Receiver.exe` を起動。セッションが生成され、QRコードと「CABLE Input」が表示されることを確認。
4. **ブラウザ送信ページ起動**: スマートフォンまたは別ブラウザでQRコードURLを開き、カメラ/マイク許可後「送信開始」を押下。
5. **映像・音声確認**:
   - Receiverのプレビュー画面にリモートカメラ映像が表示されること。
   - Windows **Camera アプリ** または **OBS Studio** で `WebRTC Bridge Windows Virtual Camera` を選択し、720p30映像が受信されていること。
   - 録音アプリ等で `CABLE Output` を選択し、ブラウザからの音声が届いていること。
6. **終了処理**: Receiverを終了し、Cameraアプリの一覧から仮想カメラが消滅することを確認。
