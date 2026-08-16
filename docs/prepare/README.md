# WebRTC Windows 11 Virtual Camera PoC 実装パッケージ

調査・設計基準日: **2026-08-16**

このパッケージは、ブラウザから送信された映像・音声をWindows 11アプリでWebRTC受信し、次の出力として利用できるPoCを実装するための仕様書一式です。

- 映像: Windows 11 Media Foundation Virtual Camera
- 音声: 既存のVB-CABLE等の仮想オーディオケーブル
- ペアリング: Windows受信アプリが表示するQRコード
- シグナリング: Cloudflare Workers + SQLite-backed Durable Objects
- 通信方式: HTTPS REST API + 短時間ポーリング
- ICE交換: 初期版はNon-Trickle ICE
- メディア: Cloudflareを経由せずWebRTC P2Pを優先し、必要時のみTURNを利用

## 最初に読む順序

1. `docs/00_project_brief.md`
2. `docs/01_requirements_and_scope.md`
3. `docs/02_architecture.md`
4. `docs/03_session_and_signaling_protocol.md`
5. `docs/04_cloudflare_worker_design.md`
6. `docs/05_browser_sender_design.md`
7. `docs/06_windows_receiver_design.md`
8. `docs/07_virtual_camera_design.md`
9. `docs/08_vb_cable_audio_design.md`
10. `docs/09_security_and_privacy.md`
11. `docs/10_test_and_acceptance.md`
12. `docs/11_delivery_plan.md`
13. `docs/12_risks_and_decisions.md`
14. `prompts/CODEX_IMPLEMENTATION_PROMPT.md`

## 主要な設計判断

| 項目 | 採用方針 |
|---|---|
| シグナリングの状態保存 | 1セッションにつき1つのSQLite-backed Durable Object |
| Webページ配信 | Worker Static Assets。同一オリジンでAPIも提供 |
| SDP/ICE | ICE gathering完了後のSDPを丸ごと交換 |
| ポーリング | 1秒間隔から開始し、最大2秒。全体タイムアウト60秒 |
| セッション有効期限 | 作成時から5分。ポーリングでは延長しない |
| Offer生成側 | ブラウザ送信側 |
| Answer生成側 | Windows受信側 |
| QRコード | Join URLを格納。Join TokenはURLフラグメント内 |
| Windows WebRTC | upstream native libwebrtcを固定リビジョンで利用 |
| 仮想カメラ | `MFCreateVirtualCamera` + カスタムMedia Foundation Media Source |
| 仮想カメラ形式 | v1は1280×720、30fps、NV12固定 |
| アプリ→Media Source IPC | v1は名前付きパイプ。将来共有メモリへ置換可能 |
| 音声 | libwebrtc AudioDeviceModuleからVB-CABLEのrender endpointへ出力 |
| UI | 依存を抑えたC++20 Win32デスクトップアプリ |
| QR生成 | 小さなQRエンコーダーライブラリを利用。ブラウザ内スキャンは実装しない |
| 初期対象外 | WebSocket、SSE、D1、KV、Trickle ICE、録画、独自仮想マイク、低遅延最適化 |

## ZIP内の成果物

```text
.
├── README.md
├── docs/
│   ├── 00_project_brief.md
│   ├── 01_requirements_and_scope.md
│   ├── 02_architecture.md
│   ├── 03_session_and_signaling_protocol.md
│   ├── 04_cloudflare_worker_design.md
│   ├── 05_browser_sender_design.md
│   ├── 06_windows_receiver_design.md
│   ├── 07_virtual_camera_design.md
│   ├── 08_vb_cable_audio_design.md
│   ├── 09_security_and_privacy.md
│   ├── 10_test_and_acceptance.md
│   ├── 11_delivery_plan.md
│   ├── 12_risks_and_decisions.md
│   └── 13_reference_sources.md
├── specs/
│   ├── openapi.yaml
│   ├── session_state_machine.mmd
│   ├── connection_sequence.mmd
│   ├── frame_pipe_protocol.md
│   ├── shared_memory_future_design.md
│   ├── config.example.json
│   ├── cloudflare.env.example
│   └── repository_layout.md
├── prompts/
│   ├── CODEX_IMPLEMENTATION_PROMPT.md
│   └── CODEX_VERIFICATION_PROMPT.md
├── checklists/
│   ├── ACCEPTANCE_CHECKLIST.md
│   ├── WINDOWS_DEV_ENV_CHECKLIST.md
│   └── CLOUDFLARE_DEPLOYMENT_CHECKLIST.md
└── MANIFEST.json
```

## 実装開始方法

コーディングエージェントへ、展開したディレクトリ全体と次のファイルを渡してください。

```text
prompts/CODEX_IMPLEMENTATION_PROMPT.md
```

エージェントには、プロンプトだけでなく本パッケージ全体を読み取り可能にしてください。実装後の別エージェントによるレビューには、次を使用します。

```text
prompts/CODEX_VERIFICATION_PROMPT.md
```

## 前提と注意

- QRコードはWindows画面上のURLをスマートフォン等のOSカメラで読み取り、ブラウザで送信ページを開く方式です。Webページ内のQRスキャナーではありません。
- HTTPSはシグナリングに使用します。映像・音声はWebRTCのICE/DTLS-SRTPで別経路を通ります。
- STUNだけでは一部ネットワークで接続できません。Cloudflare Realtime TURNの短期資格情報をWorkerから発行できる拡張点を最初から設けます。
- Media Foundation Virtual CameraのMedia Source DLLはFrame Server側のプロセスへロードされます。受信アプリと同一プロセスではないため、明示的なIPCが必要です。
- VB-CABLEは本プロジェクトに同梱・再配布しません。ユーザーが別途導入する前提です。
- upstream libwebrtcは安定したC/C++ SDKではなく、ビルド要件も変化します。必ず動作確認済みコミットを固定し、ビルド条件をロックファイルへ記録します。
