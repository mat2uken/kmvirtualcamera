# 01 コード監査: READMEからは分からない実装

## 調査の基準と読み方

基準コミットは `1f22e7423748a1de6469e40ef5516a29d9b19fa8`。この文書は静的コード調査です。
「リスク」は問題を生む入力・並行実行があり得るという判断で、実環境で発生を再現した主張ではありません。
新しい共通DataChannel実装に対してのみ、回帰単体試験とsanitizer試験を実施しました。
下記のパスは特記しない限り基準コミットのものです。履歴は `git show <基準コミット>:<path>` で確認できます。

## 実際の映像経路

ブラウザには通常のMediaTrackとWebCodecs＋unreliable DataChannelの2経路があります。
`cloud/web/src/rtc.ts` が選択とカメラ切り替えを担い、`webcodecs_sender.ts` がH.264、分割送信、bitrate／PLI制御を実装しています。
Windows側は `receiver/rtc/peer_connection_manager.cpp` が各経路を受け、デパケタイザ→圧縮AUキュー→デコードへ接続します。

`windows/receiver/app/app_controller.cpp::VideoWorkerProc` の主経路はCPU版 `DecodeAccessUnit` →CPU NV12変換→`PublishFrame`→プレビューです。
GPU用API・共有textureクラスが存在するだけで、主経路が全面的にCPUコピーなしとは判断できません。
`windows/receiver/media/pipe_publisher.cpp` はDXGI共有テクスチャ、共有メモリ、Named Pipe用の更新を持っています。
macOS側へこれら3種類のIPCを再現するのではなく、標準sink streamを優先します。

## 今回のコードに反映した修正

| ID | 根拠・問題 | 修正と試験 |
|---|---|---|
| A01 | `dc_video_depacketizer.cpp::RecordH264AccessUnit` は受信AUを `debug_stream_dump.h264` と `.jsonl` へ無条件に書き、毎回flushする | 共通実装から保存処理を削除。試験中に当該ファイルを生成しないことを確認。リポジトリ全体の保存機能を廃止したという意味ではない |
| A02 | 同一frameSeqの最初のtotalChunksでvectorを作成後、後続パケットのtotalChunksとの一致を検証せずindex参照 | count／timestamp／固定flagの整合を先に確認。count変更入力と重複競合の回帰試験を追加 |
| A03 | 初回キーフレームを見つけられない場合、Drainが期限切れ掃除前にreturnする | 未初期化でも期限処理。32フレーム・2MiB payload上限。初回IDR未受信の大量入力を試験 |
| A04 | packed構造体へのreinterpret_castでwireを読む。payload長の明示的上限がない | little-endianをbyte単位で読む。magic／flags／先頭末尾／index／payload長を検証 |
| A05 | C++定義はpayload1168、実ブラウザの送信はpayload1180 | 定義を実装に合わせ1180、packet1192へ統一。従来受信が常に切り詰めていたわけではなく、仕様定義の不一致 |
| A06 | mutex保持中にframe/control callbackを呼ぶ | callback内容を保持してlock外で通知。callbackからResetを呼ぶ回帰試験。並行破棄まで安全にするものではない |
| A07 | SPS/PPSの簡易検出、headerのkeyフラグへの依存 | 実NALのIDRを確認、SPS/PPS対を検証して更新。部分更新拒否・AUD付加・Annex-B／4-byte AVCCの構文境界を整理 |

これらの実装は `shared/receiver/codec/dc_video_depacketizer.*`。
Windowsの元パスはこの1実装へ転送します。別のWindows専用コピーは残していません。

## 依然として修正が必要な項目

以下は「検出済み・未修正」です。共通ディレクトリへ移動しただけのコードを、検証済みの安全なコードと見なさないでください。

| ID / 優先 | 根拠箇所 | 問題と次の対応 |
|---|---|---|
| R01 / リリース前必須 | `windows/receiver/codec/h264_rtp_depacketizer.cpp::ProcessRtpPacket` | STAP-A内NAL長0を拒否せず、長さ欄直後を読み得る。型・length・paddingの検証を完全化しsanitizer回帰試験を追加 |
| R02 / リリース前必須 | 同上 | 古い／重複RTPのsequence更新は抑えても、その後のpayload処理は続く。AU混入防止とreorder方針を明文化。AU／FU蓄積に明示的上限が必要 |
| R03 / リリース前必須 | `windows/receiver/rtc/enhanced_rtcp_session.cpp::incoming` | 共通RTCPヘッダー長だけを確認しSR／RRへcast。型固有長・compound packet長・report countを先に検証する |
| R04 / リリース前必須 | `peer_connection_manager.cpp` のaudio onFrame、`app_controller.cpp` のaudio callback | 受信した圧縮音声をPCM16へcastしている。さらにサンプル要素数とチャンネル数の二重積でspan長を過大にするリスク。音声を正しく復号しframe数／要素数を分離 |
| R05 / 統合前 | `app_controller.cpp::Initialize`, `SignalingWorkerProc`, `VideoWorkerProc` | worker開始、デコーダ初期化、再初期化が別スレッド。再初期化ではD3D device引数も抜ける。decoder操作を単一ownerに集約 |
| R06 / 統合前 | `peer_connection_manager.cpp` の映像callback | RTP90kHz値とDataChannelのmicrosecondsが同じtimestampUs型へ入る。時刻のdomain明示・unwrap・世代resetを接続 |
| R07 / 統合前 | `app_controller.cpp::VideoWorkerProc`, `TestPatternWorkerProc` | 通常出力はcount×16666、待機映像はGetTickCount64×1000とsleep33ms。受信時刻・送信時刻・出力時刻を分離し有理数pacerへ |
| R08 / 統合前 | `receiver/app/lockfree_h264_queue.h` とPush呼び出し | 64×256KiBを予約、満杯時Push失敗を呼び出し元が扱わない。SPSCのproducer数も保証が必要。圧縮参照関係を壊さない過負荷復旧を実装 |
| R09 / ネットワーク検証前 | `win_http_client.cpp`, `peer_connection_manager.cpp::Initialize` | ICE設定の読取り・組立てがSTUN中心。TURN(s)と認証情報、IPv6等を完全に引き継ぎ、ライブラリのURL処理を利用 |
| R10 / 統合前 | `win_http_client.cpp` | HTTP実行と手書きJSONが混在。文字列検索は完全なJSON処理ではなく、応答蓄積にも制限が必要。共通schemaとHTTP実行を分ける |
| R11 / リリース前必須 | `cloud/web/src/webcodecs_sender.ts::flushPendingAccessUnit` | totalChunksをUint8へ書く前の255超チェックがない。AU全体の長さ上限を送信前に検証。途中まで送ってから止めない |
| R12 / 安定化前 | `peer_connection_manager.cpp` とDC timer契約 | 無通信時も動く独立したDC期限処理が必要。到着callbackだけに期限掃除・PLI再送を依存させない |
| R13 / 安定化前 | RTC callbackとClose/Reset | raw this捕捉、shared_ptrやコールバックの世代切替を整理。Close直後のコールバックが新セッションへ到達しない構造にする |

`LockFreeH264Queue` の16MiBは予約容量からの計算で、プロセスRSS測定値ではありません。
`EvaluateDelayGradientBwe` は名前と異なり、確認したコードでは主に時間ごとの加算探査と損失時減算です。GCC相当の実装と説明しないでください。

## 変更しなかった領域

クラウド、ブラウザ、Windows仮想カメラDLL、WinHTTP、AppController、PeerConnectionManager、音声実装は今回の主たる変更対象外です。
RTP／RTCP／帯域推定の抽出は既存blobと同内容の移動です。修正済みA項目を、R項目まで解消済みと解釈しないでください。

## 調査根拠を確認する手順

```sh
git show 1f22e7423748a1de6469e40ef5516a29d9b19fa8:windows/receiver/codec/dc_video_depacketizer.cpp
git show 1f22e7423748a1de6469e40ef5516a29d9b19fa8:cloud/web/src/webcodecs_sender.ts
git diff 1f22e7423748a1de6469e40ef5516a29d9b19fa8..HEAD -- shared windows/receiver windows/common
```
