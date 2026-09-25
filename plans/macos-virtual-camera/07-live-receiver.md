# 段階7: ブラウザ映像をMac仮想カメラへ接続

目的は既存ブラウザの2つの映像経路を、共通受信処理、VideoToolbox、host側sink publisherへつなぐこと。Cloudflareのsession/Offer/Answer APIとブラウザの送信方式を維持する。段階3でRTCの実依存と回帰、段階6でhost→sink→sourceの動作が確認できていることを開始条件にする。

## 接続する経路

```text
Browser MediaTrack H.264 ─┐
                          ├→ Cloudflare signaling → host RTC → Annex-B AU
Browser WebCodecs/DC ─────┘                                ↓
                                  VideoToolbox → 420v → CMIO sink publisher
                                                    → Extension sink → source
                                                    → camera consumer
```

`cloud/web/src/rtc.ts` はMediaTrack H.264を既定とする。DataChannel経路は `webcodecs_sender.ts` と `dc_packetizer.ts` が担う。DCの12 byte header、1180 byte payload、1 AU 300900 byte上限は[メディア資料](../../docs/macos/03_MEDIA_AND_PROTOCOLS.md)を参照する。同資料の「AU上限対策未追加」「timer配線なし」は旧記述である。適用後のコードと[対策記録](../../docs/macos/09_REVIEW_FIXES.md)を確認する。

## 実装手順

1. `shared/receiver/signaling/session_client.h` のMac向け `IHttpTransport` を実装する。`IHttpTransport` は `shared/km/receiver_contracts.h` にある。HTTPS検証、redirect禁止、期限・cancel、1MiB応答上限を守る。通信待機は専用signaling workerで行い、UI・RTC callback・video workerを待たせない。session作成、Offer poll、Answer送信、削除を既存APIと照合する。
2. `windows/receiver/rtc/peer_connection_manager.*` と `shared/receiver/rtc` の依存を整理する。session、RTC、packet→AU、世代・時刻変換、状態・統計を `ReceiverEngine` 相当の共通C++処理へ集約する。Win32 UI型、D3D、CVPixelBufferを共通公開型へ入れない。Windows転送 `.cpp` と共通ライブラリを二重リンクせず、段階3の項目で回帰を確認する。
3. `km::EncodedVideoFrame` のowning Annex-B AUをMac video workerへ渡す。MediaTrack RTPではH.264 reassembly、DCでは既存chunk再構成・期限掃除・PLIを使う。SPS/PPSを伴うIDRまで復旧待ちし、欠損・queue満杯・解像度変更で古い依存列を捨てる。RTP 90kHz、DC microseconds、受信monotonic、Mac host clockを別々に扱う。
4. Macの `IVideoPipeline` 実装でdecode、向き、geometry、色、420v正規化を行う。段階6のpublisherへ最新フレームを渡す。現状の `VideoToolboxDecoder` はAUごとに完了を待つ。接続後に実測し、段階8でbounded非同期化する。format変更とstopではworkerを直列化し、旧世代callbackを失効させる。
5. AppKit hostへ接続操作、QR/join URL、接続状態、カメラ導入状態、映像供給状態、capture状態、エラー、fps・遅延統計を表示する。プレビューを閉じたり最小化したりしてもカメラ供給を継続し、明示的stopでは旧映像を破棄する。QR表示は既存Windows/ブラウザの入出力形式と照合する。
6. TURN設定の `urls`、`username`、`credential`、policyをそのままMac RTCへ引き継ぐ。既定libjuiceにないTURN TCP/TLS対応をUIや資料で主張しない。relay-only時に使えるUDP TURNがない場合は理由付きで失敗させる。

### Mac先行（M4-a）の到達

手順1（`IHttpTransport`）、手順3–5の接続前部分（owning AU→Mac video worker、映像パイプラインとsink供給、join UI・接続状態・統計表示）、手順6（TURN引き継ぎ）はM4-a単位1–8で実装・試験済み。結果は[00-plan](00-macos-first-plan.md)のM4-a節に記録する。手順2の `ReceiverEngine` への集約はWindowsビルド回帰が必要なためM4-b依存で延期。接続後の実測（実映像のfps・遅延、統計のConnected時値）はこの段階で行う。

## 実映像の確認順

まずローカルのsignalingとブラウザでMediaTrackをつなぎ、host preview、publisher投入、source captureの各段階に同じ動く映像があるか確かめる。次にWebCodecs/DCで同じ順に確認する。ブラウザのカメラ切替、portrait/landscape、SPS/PPS変更、回線断、再接続、AU上限超過、音声無効設定も試す。Cloudflareを使用する構成では、secret値を残さずAPI応答、Offer/Answer、ICE状態、選択candidate pairを記録する。

## 完了条件

実ブラウザのMediaTrackとWebCodecs/DCの両方で、一般アプリのMac仮想カメラに映像が届く。セッション開始・終了、失敗表示、カメラ切替、通信断・再接続、古いcallback破棄、黒画面へのtimeoutが確認できる。各試験でブラウザ種類・版、OS、source解像度と向き、実行SHA、映像記録、ログ、統計を残す。Windowsの同じ2経路も後退していないことを確認する。

## 段階8へ渡す測定値

ここで初めて到達時刻と実遅延を測る。capture→encode→受信→decode→sink→sourceの時刻とqueue深さを記録し、待ち時間の支配箇所を特定する。AU単位waitを含む現構成の値を基準とし、根拠なく「低遅延」や「ゼロコピー」と記載しない。
