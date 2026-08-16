# 02. システムアーキテクチャ

## 1. 全体構成

```text
┌──────────────────────────────┐
│ Sender Browser               │
│                              │
│  getUserMedia                │
│  RTCPeerConnection           │
│  Offer / ICE gathering       │
└──────────────┬───────────────┘
               │ HTTPS REST + polling
               ▼
┌────────────────────────────────────────────────────┐
│ Cloudflare                                         │
│                                                    │
│ Worker                                             │
│ ├─ Static Assets: /send/                           │
│ ├─ REST API: /v1/...                               │
│ ├─ Security headers / validation                   │
│ └─ Optional TURN credential generation            │
│                  │                                 │
│                  ▼                                 │
│ SQLite-backed Durable Object                       │
│ └─ One object per signaling Session                │
└──────────────┬─────────────────────────────────────┘
               │ HTTPS REST + polling
               ▼
┌────────────────────────────────────────────────────┐
│ Windows Receiver App                               │
│                                                    │
│ Session/QR UI                                      │
│ Signaling Client                                   │
│ native libwebrtc                                   │
│ ├─ Video sink → Preview / Frame pipeline           │
│ └─ Audio playout → VB-CABLE render endpoint        │
│                        │                           │
│                        │ Named Pipe, NV12 frames   │
│                        ▼                           │
│ Virtual Camera Media Source DLL                    │
│ └─ Media Foundation / Frame Server process         │
└────────────────────────────────────────────────────┘

Browser ═════ ICE + DTLS-SRTP + RTP/RTCP ═════ Windows Receiver
          direct UDP preferred; TURN fallback
```

重要点:

- Cloudflareは接続開始の情報だけを扱う。
- 映像・音声の通常経路はCloudflare Worker/DOを通らない。
- ReceiverはインターネットからlistenするHTTP/WebSocketサーバーを持たない。
- SenderとReceiverはどちらもCloudflareへoutbound HTTPSアクセスする。
- WebRTCのmedia経路自体はUDP/TCP/TURNを使用し得るため、「HTTPSだけで映像を送る」構成ではない。

## 2. コンポーネント境界

### 2.1 Cloudflare Worker

責務:

- Static Assets配信
- REST route
- Content-Type、body size、schema validation
- request ID付与
- Session IDからDurable Object stubを取得
- secret管理
- TURN credential APIの呼び出し
- レスポンスセキュリティヘッダー

持たない責務:

- SDP意味解析
- media relay
- 長期データ保存
- WebSocket接続
- ユーザーアカウント

### 2.2 Session Durable Object

責務:

- 1つのSession状態
- Token検証
- 状態遷移の排他制御
- Offer/Answerの短期保存
- Session期限切れAlarm
- 一度だけのclaim
- 期限切れ・close後の拒否

1つのSessionに関する更新が1つのDurable Objectへ集約されるため、D1上の条件付きUPDATEや分散lockを避けられる。

### 2.3 Browser Sender

責務:

- URL fragment解析
- camera/microphone permission
- local preview
- Session claim
- RTCPeerConnection作成
- Offer作成とICE gathering完了待ち
- Offer PUT
- Answer GET polling
- connection state表示
- Stop/cleanup

### 2.4 Windows Receiver

責務:

- Session作成
- QR生成・表示
- Offer polling
- PeerConnection Answer
- video/audio受信
- preview
- audio endpoint選択
- Virtual Camera lifecycle
- frame変換・IPC
- stats/diagnostics

### 2.5 Virtual Camera Media Source

責務:

- Media FoundationのMedia Source/Stream契約
- 720p30 NV12 media type
- pipe接続
- 最新フレーム保持
- timestamp/duration生成
- RequestSampleへの非ブロッキング応答
- 無入力時のblack frame
- shutdown

禁止事項:

- libwebrtcへの依存
- Cloudflare APIへのアクセス
- Receiver UIへの直接依存
- network access
- pipe readをMedia Foundation callback内で同期実行

## 3. デプロイ単位

```text
Cloud:
  1 Cloudflare Worker deployment
    ├─ Worker code
    ├─ Static sender assets
    └─ Durable Object class migration

Windows:
  Receiver.exe
  ReceiverCore.dll            optional
  VirtualCameraMediaSource.dll
  VirtualCameraRegistrar.exe  or receiver-integrated registrar
  config.json
  licenses/
  install-dev.ps1
  uninstall-dev.ps1
```

VB-CABLEは別製品としてユーザーがインストールする。

## 4. なぜDurable Objectか

短命なOffer/Answer保存だけならD1でも実装できる。しかし、以下の理由でSession単位のDurable Objectを採用する。

- Session IDから一意のstate holderへ直接routeできる。
- claim、offer、answerの競合をSession内で直列化しやすい。
- 強整合なstorageを利用できる。
- 5分後のAlarmで削除しやすい。
- SQL schema/migrationをSession dataのためだけに設計する必要がない。
- Polling APIでもデータ反映遅延を避けやすい。

KVは結果整合性のため、この用途では採用しない。D1は将来、アカウントや端末一覧等の永続データが必要になった段階で追加する。

## 5. なぜNon-Trickle ICEか

初期版では接続開始時間より実装の単純性を優先する。

```text
createOffer
  → setLocalDescription
  → ICE gathering completeを待つ
  → candidate入りlocalDescription.sdpをPUT
```

Receiverも同様にAnswer側で候補収集完了後にPUTする。

利点:

- Candidate用APIが不要
- Candidate generation/revision管理が不要
- Out-of-order candidate処理が不要
- polling endpointがOffer/Answerだけになる
- 再現性の高いPoCを作りやすい

欠点:

- 全candidate収集完了まで接続確認を始められない
- TURN/TCP等、応答しない候補があると開始が遅くなる可能性
- ICE gathering timeout設計が必要

接続後のmedia latencyには直接影響しない。将来の接続時間改善時にTrickle ICEへ拡張する。

## 6. なぜ名前付きパイプIPCか

Virtual Camera Media SourceはFrame Server側へロードされ、Receiverとは別プロセス・別セキュリティコンテキストになり得る。

v1では次を優先する。

- Windowsのsession namespaceをまたいで接続しやすい
- Receiverを管理者権限で実行しなくてよい
- 接続・切断を明示的に検出できる
- protocolをversion化できる
- 低遅延最適化前の720p30 PoCとして十分なthroughputを期待できる

フレーム転送量は1280×720 NV12 30fpsで約41.5MB/s。ローカルIPCとしてPoC可能な範囲だがcopyコストはある。将来はcontrol/heartbeatだけpipeに残し、payloadを共有メモリまたはD3D shared textureへ移す。

## 7. Threadモデル

### Browser

- JavaScript main thread
- WebRTC/media内部threadはbrowser管理
- pollingはAbortControllerで停止可能

### Receiver

```text
UI thread
  ├─ window/message loop
  ├─ state表示
  └─ preview presentation

Signaling worker
  └─ HTTPS request/poll

libwebrtc threads
  ├─ network
  ├─ worker
  └─ signaling

Video sink callback
  └─ 最新frame slotへ投入。重い処理を直接行わない

Frame conversion worker
  ├─ rotation/scale
  ├─ I420→NV12
  ├─ preview更新
  └─ pipe publisherへ最新frameを渡す

Pipe writer
  └─ 1件のwrite中でもqueueを伸ばさず次は最新frame

Audio/WebRTC ADM
  └─ 指定render endpointへplayout
```

### Virtual Camera Media Source

```text
COM/MF callback threads
  └─ RequestSample/Start/Stop/Shutdown

Pipe reader thread
  └─ frame packet受信・検証・latest buffer更新

MF work queue
  └─ 30fps sample scheduling
```

## 8. Failure domain

| 障害 | 影響範囲 | 方針 |
|---|---|---|
| Worker API停止 | 新規接続不可 | 既存P2P mediaは継続可能 |
| Durable Object期限切れ | Signaling再利用不可 | 新しいQR/Sessionを作成 |
| TURN API失敗 | relay候補なし | STUNのみで続行するか明示エラー |
| Browser permission拒否 | Sender開始不可 | Sessionを閉じて再試行 |
| WebRTC failed | media停止 | 新Session作成を案内 |
| VB-CABLEなし | 仮想mic不可 | device選択UIで警告 |
| Virtual Camera DLL未登録 | 仮想camera不可 | preview/WebRTCは継続可能 |
| Camera consumerなし | pipe client不在 | Receiverはフレームを捨てる |
| Receiver終了 | pipe切断・VC停止 | Sender PeerConnectionも終了 |
