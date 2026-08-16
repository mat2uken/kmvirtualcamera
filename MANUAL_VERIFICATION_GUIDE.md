# KM Virtual Camera - 完全手動動作確認ガイド

本ガイドでは、**Cloudflare への本番デプロイ**、**Windows 仮想カメラの登録**、**スマートフォンからの映像・音声送信**、および **Windows PC での仮想カメラ動作確認** までの一連の流れを手動で確認する手順を説明します。

---

## 全体概要図

```
[スマートフォン] (iOS Safari / Android Chrome)
      │
      │ 1. QRコードをカメラでスキャン
      │ 2. 「送信開始」をタップ (720p30 H.264/VP8 & 48kHz Opus)
      ▼
[Cloudflare Workers & Durable Objects] (HTTPS Signaling)
      │
      │ 3. Non-Trickle ICE / SDP 交換
      ▼
[Windows 11 Native Receiver (Receiver.exe)]
      │
      ├─► Direct3D 11 プレビューウィンドウ (低遅延映像表示)
      ├─► WASAPI Playout ──► VB-CABLE [CABLE Input] (仮想マイク)
      └─► Named Pipe IPC (\\.\pipe\WebRtcBridge.VirtualCamera.v1)
               │
               ▼
      [Media Foundation COM DLL (VirtualCameraMediaSource.dll)]
               │
               ▼
      [Windows カメラアプリ / OBS / Zoom / Teams / Google Meet]
         (カメラ一覧に「KM Virtual Camera」が表示・動作)
```

---

## 事前準備

### 1. 動作環境の自動チェック
PowerShell で以下の診断スクリプトを実行し、PC のセットアップ状態を確認します:

```powershell
pwsh -File scripts/verify_manual_setup.ps1
```

---

## 手順 1: Cloudflare への本番デプロイ

### 1.1 Cloudflare へのログイン (初回のみ)
```powershell
cd cloud
npx wrangler login
```
ブラウザが開き、Cloudflare アカウントの認証を行います。

### 1.2 デプロイの実行
```powershell
cd ..
pwsh -File scripts/deploy_cloud.ps1
```
デプロイが完了すると、公開 URL がターミナルに表示されます。
例: `https://kmvirtualcamera-signaling.<your-subdomain>.workers.dev`

---

## 手順 2: Windows 仮想カメラ COM DLL の登録

管理者権限で PowerShell を開き、以下を実行します（初回のみ）:

```powershell
pwsh -File scripts/register_vcam.ps1
```

> [!NOTE]
> 成功すると「Virtual Camera COM Media Source registered successfully.」と表示されます。
> （解除したい場合は `pwsh -File scripts/unregister_vcam.ps1` を実行します）

---

## 手順 3: Windows 受信アプリ (Receiver) の起動

Cloudflare の公開 URL を引数に指定して起動します:

```powershell
pwsh -File scripts/run_receiver.ps1 -SignalingUrl "https://kmvirtualcamera-signaling.<your-subdomain>.workers.dev"
```

※ ローカル開発環境（`http://127.0.0.1:8787`）でテストする場合は `-SignalingUrl` を省略またはローカルURLを指定できます。

- 起動すると、画面左側に **QR コード** と **Join URL** が表示され、シグナリング待機状態になります。

---

## 手順 4: スマートフォンからの送信操作

1. スマートフォンの標準カメラアプリで、PC 画面上の **QR コードをスキャン** します。
2. ブラウザ（iOS Safari または Android Chrome）で送信ページが開きます。
3. カメラおよびマイクのアクセス許可を「許可」します。
4. **「送信開始」** ボタンをタップします。
5. 接続ステータスが `Connecting...` から `Connected` に変わります。

---

## 手順 5: Windows 側での動作確認

### 5.1 受信アプリ内のプレビュー確認
- `Receiver.exe` の画面右側に、スマートフォンのカメラ映像（720p 30fps）が低遅延でリアルタイム描画されていることを確認します。

### 5.2 Windows 仮想カメラの確認 (カメラアプリ / OBS 等)
1. Windows 標準の **「カメラ」アプリ** を起動します:
   ```powershell
   Start-Process "microsoft.windows.camera:"
   ```
2. カメラ切り替えボタンで **「KM Virtual Camera」** を選択します。
3. スマートフォンのカメラ映像が Windows カメラアプリ上に遅延なく表示されることを確認します。
4. ※ OBS Studio や Zoom、Google Meet、Teams のカメラ選択肢にも **「KM Virtual Camera」** が現れ、同様に映像が投影されます。

### 5.3 仮想マイクの確認 (VB-CABLE)
- VB-CABLE がインストールされている場合、スマホのマイク音声が `CABLE Input` へ自動出力されます。
- Windows の「サウンドの設定」>「録音」タブで `CABLE Output` のレベルメーターがスマホの声に合わせて振れることを確認できます。

---

## 終了手順

1. スマートフォンのブラウザで **「停止」** をタップ、またはブラウザを閉じます。
2. Windows 側の `Receiver.exe` ウィンドウを閉じます（自動的にセッションおよび Named Pipe が安全に解放されます）。
