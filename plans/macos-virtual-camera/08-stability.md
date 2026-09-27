# 段階8: 映像品質、遅延、寿命の安定化

段階7の両映像経路を動かし、同じSHAと環境で問題を測る。対象はMacのdecoder、正規化、video worker、host publisher、frame relay、ブラウザ送信部。[既存の試験計画](../../docs/macos/06_TEST_PLAN.md)も参照する。

## 開始時記録（実測）

| 項目 | 値 |
|---|---|
| repo | `kmvirtualcamera-macos-coremediaio-foundation` |
| branch | `feature/macos-coremediaio-foundation` |
| HEAD | `c67916f`（remoteと同一、push済み） |
| 未コミット差分 | S1計測器4ファイルと本記録、`work/`（未追跡） |
| OS / CPU | macOS 26.7 (25G229) / Apple M3 Max (arm64) |
| Xcode / SDK | Xcode 26.6 (17F113) / macOS SDK 26.5 |
| compiler / CMake | Apple clang 21.0.0 / CMake 4.3.4 |
| 導入状態 | `KM Virtual Camera` が `system_profiler SPCameraDataType` の一覧に出る |
| 決定待ち | fps・遅延・CPU/GPU・メモリ・対象OSの許容値は未決定（製品判断待ち） |

この表のHEAD・差分・環境を単位を始めるたびに更新し、[記録様式](verification.md)に試験結果を紐付ける。

## 実施単位

| 単位 | 内容 | 到達点 |
|---|---|---|
| S1 | decode時間・queue深さ・遅延の計測器と現状構成の基準値 | AU単位waitの実測値 |
| S2 | boundedな非同期decode、世代付きcallback、drainと失効 | 未完了callbackの破棄とS1との比較 |
| S3 | `Normalize720p` とCVPixelBufferPool等の候補の比較 | copy回数とCPU負荷の実測値 |
| S4 | 幾何・色・時刻 | 向き・画角・色・PTS単調の実画像 |
| S5 | 30分以上の連続送信 | fps・遅延・queue・drop・CPU・メモリの時系列 |
| S6 | 停止・再開、障害、3 consumer | 古い映像とqueue増加がないこと |

S2はS1の値がなければ着手しない。S3もS1の値が基準になる。S4以降はS2とS3の変更を含むSHAで実施する。

## 結果記録

### S1 計測器と基準値

| 項目 | 内容 |
|---|---|
| 段階・試験ID | 段階8 S1（decode/queue/遅延の計測器と現状構成の基準値） |
| 目的・合格条件 | decode時間・queue深さ・遅延を測れる計測器を実装し、現状構成の実測値を残す |
| 結果 | 成功（計測器と基準値を取得。実映像経路で不具合1件を発見し、後に記す） |
| repo | `kmvirtualcamera-macos-coremediaio-foundation` |
| branch | `feature/macos-coremediaio-foundation` |
| HEAD | `c67916f`（計測器は未コミット差分） |
| 未コミット差分 | 計測器4ファイル、本記録、`work/`（未追跡） |
| 日時 | 2026-09-27 10:58–16:46 JST |
| OS / CPU | macOS 26.7 (25G229) / Apple M3 Max (arm64) |
| Xcode・SDK / compiler | Xcode 26.6 (17F113) / macOS SDK 26.5 / Apple clang 21.0.0 |
| ブラウザ | Google Chrome 153.0.8010.53（CDP 9222） |
| network | `wrangler dev` 127.0.0.1:8787 と `turnserver` |
| 計測経路 | in-process bench と実ブラウザ MediaTrack |

計測器の変更は `app_delegate.mm`、`video_pipeline.h`、`video_pipeline.mm`、`video_pipeline_test.mm` の4つである。本記録も同じコミットに含む。

計測器は `PipelineStats` に永続カウンタを追加した。`queueHighWater` はpush直後のqueue深さの最大値である。`decodeUs*` はworkerの全 `decode()`、`submitToPublishUs*` は `submit()` 入口からハンドラまでである。hostは毎秒 `KMAppDelegate: pipe` 行を出力する。計測は `km_macos_pipeline_test` の `--bench` と `--decodeprobe` を使った。新規targetとctest登録は増やしていない。

ハーネスのNALフレーミングは長さプレフィックス主判定に直した。`HasSlice`・`DescribeNalus`・`sampleDiag`・`nalLengthSize=` の出力を足す。製品側の `cloud/web/src/webcodecs_sender.ts` は `avc: { format: "annexb" }` で誤りなしを確認した。

#### S1-a 基準値（300枚、いずれも exit 0）

| 構成 | 条件 |
|---|---|
| `--decodeprobe 300` | decodeのみ |
| `--bench 300 30 0` | paced fps=30、capacity=8 |
| `--bench 300 30 0` burst | fps=0、capacity=300 |
| `--bench 300 30 90` | paced、回転90度 |

`--decodeprobe 300` は images=300 failed=0 silent=0 nalLengthSize=4。

paced 30 は decodeUs avg=1530.59 max=87697、toPublishUs avg=1803.86 max=87714。queueHighWater=2、published=300、workerResidualUs avg=273.27。

burst は decodeUs avg=817.037 max=70126、queueHighWater=299。toPublishUs avg=153619 max=244710、workerResidualUs avg=152802。

回転90度は letterbox 変換を挟む。decodeUs avg=1224.78 max=70584、toPublishUs avg=12823.8 max=74385、queueHighWater=2。toPublishUs は paced 30 より11020µs高く、1枚あたり約11.0msの差である。

証跡は `work/records/` の4ファイルである。`s1-decodeprobe.txt`、`s1-bench-paced30.txt`、`s1-bench-burst.txt`、`s1-bench-rot90.txt` を使った。いずれも未追跡である。

Xcode build は `BUILD SUCCEEDED`・警告0である。最終treeの証跡は `work/records/s1-xcodebuild-final.txt` にある。

#### ゲート（最終tree）

| ゲート | 結果 | 証跡 |
|---|---|---|
| `sh scripts/test_macos_foundation.sh` | 10/10 pass、警告0 | `work/records/s1-gate-foundation.txt` |
| `sh scripts/build_macos_rtc.sh` | 12/12 pass、自社コード警告0 | `work/records/s1-gate-rtc.txt` |
| `cloud` `npm test` | 18/18 pass | `work/records/s1-gate-cloud.txt` |

3ゲートは診断コードを外した最終treeで実行した。終了コードはいずれも0である。

#### S1-b 実ブラウザ映像経路

送信元は `canvas.captureStream(1280, 720, 30)` のテストダブル、mode は mediatrack、解像度720p、fps30、GOP=60。audioは実 `getUserMedia` を呼ばない。hostは `-KMStartSignaling` 起動でjoin URLを取得した。

| 試行 | 結果 | 証跡 |
|---|---|---|
| 1 | 失敗（証跡として保存） | `work/records/s1-b-cdp.txt` |
| 2 | 成功 | `s1-b-cdp2.txt`、`s1-b-host.txt` |
| 3 | 成功 | `s1-b-cdp3.txt`、`s1-b-live-host.txt` |
| 4 | 計測成功、consumerツールはクラッシュ | `s1-b-live2-host.txt` |
| 5 | 計測成功、consumer 40枚中6枚のみ実映像 | `s1-b-live3-host.txt`、`s1-b-grabs/` |

試行1はCDP注入が効かず CONNECT_TIMEOUT になった。失敗の証跡を残した。

試行2は published=2007、decodeErr=0、normalizeErr=0、queueMax=1。decodeUs avg=1245.6 max=99183、toPublishUs avg=1274.4。

試行3は published=2431、decodeUs avg=1204.5 max=112769、toPublishUs avg=1232.7、queueMax=1。

試行4は published=2029、decodeUs avg=1153.6、toPublishUs avg=2410.0。試行5は published=2029、decodeUs avg=1057.3 max=84688、toPublishUs avg=2312.8 max=84708。

試行5は queueMax=1、decodeErr=0、normalizeErr=0、queue full=0。CDP手順は `work/records/s1-b-cdp.mjs` で、試行5の出力は `s1-b-cdp5.txt` である。

試行4のクラッシュは `work/live_consumer.swift` の `Grab.captureOutput` で、製品側ではない。ログは `~/Library/Logs/DiagnosticReports/` にある。

試行5のconsumer 40枚は g01–g06 と g13–g40 が mean=0.000 の黒、g07–g12 だけが mean=137.1–140.9 の実映像である。時刻は g07 が11:32:39、g12 が11:32:49、g13 が11:32:51 で、host最終feed 11:32:48.967 と一致する。

#### S1で見つけた不具合（未修正）

実映像で `KMSinkPublisher: sample create failed (N) status=-12743` が発生し、feedが止まってconsumerが黒になる。`kCMSampleBufferError_InvalidMediaFormat` である。接続から約11秒後に始まり、以降30件/秒で続く。

| 切り分け | 結果 | 証跡 |
|---|---|---|
| 失敗フレームのattachment | 3キーの外に4つある | `work/records/s1-b-diag-host.txt` |
| 成功フレームのattachment | 3キーのみ | 同上 |
| decode出力サイズ | 960x540は成功、1280x720は失敗 | `s1-b-diag2-host.txt` |
| 時刻の一致 | 失敗再開はn=300の1280x720復帰と一致 | 同上 |

失敗フレームは `CGColorSpace` と `CVFieldCount` を持つ。さらに `CVImageBufferChromaLocationTopField` と `CVImageBufferChromaLocationBottomField` を持つ。これらは `KMCreateHostFormatDescription` の3キーに無いので、一致しない。

黒 (`MakeBlack720p`) とletterbox後の新規bufferは3キーのみなので成功する。

`960x540` のときは `Normalize720p` が新規bufferへletterbox copy（same=0）する。`1280x720` のときは恒等パス（same=1）でVT attachmentを保持し、そのまま失敗する。

失敗(1)〜(3)は16:41:48 に発生した。pipeline n=2〜150 が960x540 の間は成功した。失敗再開は n=300 が1280x720 へ戻る16:42:01 と一致する。

原因は `Normalize720p` の恒等パスがVideoToolboxのattachmentを保持したままである。送信側の解像度変更（960x540→1280x720）が発火条件になる。S1-bで映像が出るのはletterbox稼働時だけである。S5の30分連続より前に修正が必要である。

修正は `Normalize720p` とformat description のどちらかをattachment集合で一致させる。担当はS3である。

診断コードは製品コードに一時追加しただけである。全件を除去し、xcodebuild警告0と3ゲートを最終treeで再実行した。

## デコードとメモリ

1. 現在の `VideoToolboxDecoder` はAUごとに `VTDecompressionSessionWaitForAsynchronousFrames` を呼ぶ。実映像でdecode時間、queue深さ、遅延を測ったうえで、bounded outstanding decodeと世代付きcallbackへ変える。完了順が前後する場合の表示順を決め、入力を無制限に保持しない。
2. stop、format変更、カメラ切替、IDR復旧、hardware decoder切替で、未完了callbackをdrainまたは失効させてからsessionを破棄する。callback外へ渡すCVPixelBufferはretainし、UI・Extensionとの寿命を分ける。ASan/UBSanの共通試験に加え、Mac実アプリの参照数とメモリ推移を確認する。
3. `Normalize720p` のCPU基準実装と、CVPixelBufferPoolやMetal等の候補を比較する。stride付きplane、format変更、pool枯渇、copy回数、CPU/GPU負荷を測る。実測なしに完全ゼロコピーと記録しない。

## 幾何・色・時刻

portrait/landscape、90/180/270度回転、clean aperture、pixel aspect ratio、letterbox、SPS/PPS変更を試す。向き・画角・有効画像領域を実画像と比べる。恒等attachmentは拒否せず、非恒等の場合は正しい変換か理由付きの失敗にする。plane strideを画面幅と取り違えない。

入力のvideo/full range、601/709 matrix、色attachmentを読み、出力420vと選んだ色処理を画素値で確かめる。metadataを写しただけで色変換済みとしない。Mac sourceのPTS、duration 1/30、host時刻、`notifyScheduledOutputChanged:` の時刻が単調か記録する。送信端末とMacの時計は同期前提にしない。

## 寿命と負荷

30分以上の連続送信で、fps、遅延分布、queue深さ、drop/PLI、CPU/GPU、メモリ、温度による変化を一定間隔で記録する。実際の許容値は製品条件と測定結果から決め、決定前に合格と書かない。停止・再開、通信断、Extension再起動、host強制終了、スリープ復帰、ユーザー切替、更新、3 consumerの同時利用を試す。

source consumerが0でもproducerがいる間は古いsampleを滞留させない。producer消失後は現relayの1秒しきい値で待機画面へ移るか調べる。再接続後に旧世代の映像が表示されないことを確かめる。queue満杯、outstanding consume、duplicate sequence、last sampleの反復をログと映像で記録する。

## 試験環境の広げ方

初回はApple Siliconの選択したmacOS版で確認する。製品として複数macOS版を支える場合は、deployment target 12.3だけで判断せず、各版で署名済み導入、列挙、capture、停止・更新を再実行する。Intel版を提供する判断ならx86_64の全依存、署名物、実機映像を別に検証する。ブラウザごとにMediaTrackとWebCodecsの利用可否を記録する。

## 完了条件

段階7の両映像経路を保ったまま、非同期decodeのbounded容量と破棄規則、正しい向き・画角・色、時刻の単調性、30分連続時の資源推移を証拠付きで示せる。停止・再開や複数consumerで古い映像、持続的なqueue増加、クラッシュ、解放漏れがない。製品のfps・遅延・CPU/GPU・メモリの許容値と対象OSは、測定後に明記し、その値に照らして判定する。
