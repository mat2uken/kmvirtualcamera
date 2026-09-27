# 段階8: 映像品質、遅延、寿命の安定化

段階7の両映像経路を動かし、同じSHAと環境で問題を測る。対象はMacのdecoder、正規化、video worker、host publisher、frame relay、ブラウザ送信部。[既存の試験計画](../../docs/macos/06_TEST_PLAN.md)も参照する。

## 開始時記録（実測）

| 項目 | 値 |
|---|---|
| repo | `kmvirtualcamera-macos-coremediaio-foundation` |
| branch | `feature/macos-coremediaio-foundation` |
| HEAD | 開始 `c67916f`、S1 `17576ad`、S2 `5301bdf`、S3 `380ec0e` |
| 未コミット差分 | 開始時=`work/`（未追跡）。S1=`17576ad` S2=`5301bdf` S3=`380ec0e` |
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
| HEAD | 計測は `c67916f`＋未コミット差分、commitは `17576ad` |
| 未コミット差分 | 計測時点: 計測器4ファイルと本記録、`work/`（未追跡） |
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

### S2 boundedな非同期decode

| 項目 | 内容 |
|---|---|
| 段階・試験ID | 段階8 S2（boundedな非同期decode、世代付きcallback、drain） |
| 目的・合格条件 | 未完了callbackをdrainしてからsessionを破棄し、S1の値と遅延を比べる |
| 結果 | 成功（3ゲートとASan/UBSan通過、benchで遅延改善、実映像の到達も確認） |
| repo | `kmvirtualcamera-macos-coremediaio-foundation` |
| branch | `feature/macos-coremediaio-foundation` |
| HEAD | 実装は `5301bdf`、本記録は次コミット |
| 未コミット差分 | 本記録と `work/`（未追跡）のみ |
| 日時 | 2026-09-27 17:00–17:45 JST |
| OS / CPU | macOS 26.7 (25G229) / Apple M3 Max (arm64) |
| Xcode・SDK / compiler | Xcode 26.6 (17F113) / macOS SDK 26.5 / Apple clang 21.0.0 |
| ブラウザ | Google Chrome 153.0.8010.53（CDP 9222） |
| network | `wrangler dev` 127.0.0.1:8787 と `turnserver` |
| 計測経路 | in-process bench と実ブラウザ MediaTrack |

対象は6ファイルである。decoder側は `native_media.h` と `video_toolbox_decoder.mm` である。pipeline側は `video_pipeline.h` と `video_pipeline.mm` である。残りは `video_pipeline_test.mm` と `macos/CMakeLists.txt` である。

decoderは上限4枚のoutstandingウィンドウを持ち、`submit()` が投入、`takeOldest()` が結果を投入順に返す。投入順はPTS順なので、完了が前後しても後ろのフレームが先にhandlerへ出ることはない。

sessionは破棄前にdrainして世代を進めるため、未完了callbackが解放済みのslotへ書き込むことはない。slotは `reset()` でも捨てず、slotと `CMSampleBuffer` はcallback完了後にownerだけが解放する。

backpressureやstopで捨てられた連鎖の結果はhandler前に落とし、`staleEpoch` として数える。decoderの `needIdr` は投入側で下げ、取り出し側では上げるだけにした。tools用の `decode()` は1対1ラッパで、S1と同じ挙動のままである。

`macos/CMakeLists.txt` は `KM_ENABLE_SANITIZERS` のときだけdecoderとpipelineへASan/UBSanを付ける。既定はOFFで通常のビルドは変わらない。

testは `FillWindow` と `TakeOldestInOrder` の2ヘルパを足し、ウィンドウ上限・投入順・`reset()`時のdrainを検証する。既存10項目は維持した。

#### S2-a bench比較（300枚、いずれも exit 0）

| 指標 | S1 | S2 | 差 |
|---|---|---|---|
| decodeprobe images / failed | 300 / 0 | 300 / 0 | 0 |
| paced30 decodeUs avg | 1530.59 | 1506.34 | -24.25 |
| paced30 toPublishUs avg | 1803.86 | 1654.9 | -148.96 |
| paced30 queueHighWater | 2 | 1 | -1 |
| paced30 workerResidualUs avg | 273.27 | 148.567 | -124.70 |
| burst decodeUs avg | 817.037 | 920.777 | +103.74 |
| burst toPublishUs avg | 153619 | 89929.2 | -63689.8 |
| burst drainWallMs | 250 | 121 | -129 |
| burst queueHighWater | 299 | 299 | 0 |
| 回転90 decodeUs avg | 1224.78 | 1189.53 | -35.25 |
| 回転90 toPublishUs avg | 12823.8 | 8580.42 | -4243.4 |
| 回転90 workerResidualUs avg | 11599 | 7390.89 | -4208.11 |

`decodeUs` はS2以降、`submit()` 入口から取り出しまでを測る。ウィンドウ内の滞留を含む点が、S1の `decode()` 呼び出し直前の時間と異なる。burstの増加は遅延の悪化ではない。

3構成とも published=300・decodeErrors=0・normalizeErrors=0 である。burstのdrainWallMsは250→121で、300枚を吐き出す時間は半分になった。

証跡は `work/records/` の4ファイルである。`s2-decodeprobe.txt`、`s2-bench-paced30.txt`、`s2-bench-burst.txt`、`s2-bench-rot90.txt` を使った。Xcode buildの証跡は `s2-xcodebuild.txt` で、`BUILD SUCCEEDED`・警告0である。

#### ゲート（最終tree）

| ゲート | 結果 | 証跡 |
|---|---|---|
| `sh scripts/test_macos_foundation.sh` | 10/10 pass、警告0 | `work/records/s2-gate-foundation.txt` |
| `sh scripts/build_macos_rtc.sh` | 12/12 pass、自社コード警告0 | `work/records/s2-gate-rtc.txt` |
| `cloud` `npm test` | 18/18 pass | `work/records/s2-gate-cloud.txt` |
| ASan/UBSan `ctest` | 10/10 pass、指摘0 | `work/records/s2-asan-gate.txt` |

4ゲートはS2実装の最終treeで実行し、終了コードはいずれも0である。ASan/UBSanは `build/foundation-asan-macos` で、`detect_leaks` はこの環境で非対応のため外した。

decoderとpipelineも対象に入り、unit 10項目・`--decodeprobe 300`・回転90のbenchで指摘0である。

#### S2-b 実ブラウザ映像経路

| 試行 | 送信側 | 結果 | 証跡 |
|---|---|---|---|
| 1 | 1280x720 | published=2768、`-12743`=2550件、consumer黒 | `s2-b-host.txt` |
| 2 | UI解像度854x480 | 入力は1280x720のまま、`-12743`=2400件で黒 | `s2-b2-host.txt` |
| 3 | 960x540 | `-12743`=0件、published=2739、実映像 | `s2-b3-host.txt` |

試行1は accepted=2768・published=2768、decodeErr=0、normalizeErr=0、queueMax=2 である。decodeUs avg=1356.9 max=84404、toPublishUs avg=2355.4 max=84419 である。

試行2はUIの解像度を854x480へ変えたが、テストダブルが1280x720固定のため入力は変わらず、`-12743`が2400件で黒のままだった。published=2732・decodeErr=0・normalizeErr=0 である。失敗の証跡として残した。

試行3はテストダブルを960x540へ変えた。letterbox経路になり、`-12743`は0件でconsumerに実映像が出た。accepted=2739・published=2739、decodeErr=0、normalizeErr=0、queueMax=1 である。

試行3のdecodeUs avg=731.1 max=97414、toPublishUs avg=10030.5 max=106224 である。toPublishUs が高いのはletterbox copyが約11ms入るためで、S1の回転90度と同型である。

3試行とも backpressure・needKeyframe・staleGen は0、decodeErrとnormalizeErrも0である。実映像の比較はS1-b試行5の2312.8とS2-b試行1の2355.4で、差は42.6µsである。

証跡は `work/records/` にあるCDP出力3点、hostログ3点、consumer画像3点である。試行1と試行2の画像は黒、`s2-b3-consumer.png` が実映像の証拠である。consumer黒の原因はS1で特定した恒等パスの `-12743` で、S2の対象外のままである。

### S3 attachment集合の一致と正規化候補の比較

| 項目 | 内容 |
|---|---|
| 段階・試験ID | 段階8 S3（attachment集合の一致、Normalize720p候補の比較） |
| 目的・合格条件 | 恒等パスの `-12743` を消し、候補ごとにcopy回数とCPU負荷を実測する |
| 結果 | 成功（実映像で `-12743` ゼロ、MetalがCPU比17.7倍、3ゲート通過） |
| repo | `kmvirtualcamera-macos-coremediaio-foundation` |
| branch | `feature/macos-coremediaio-foundation` |
| HEAD | コードは `380ec0e`、本記録は次コミット |
| 未コミット差分 | 本記録と `work/`（未追跡）のみ |
| 日時 | 2026-09-27 18:05–19:45 JST |
| OS / CPU | macOS 26.7 (25G229) / Apple M3 Max (arm64) |
| Xcode・SDK / compiler | Xcode 26.6 (17F113) / macOS SDK 26.5 / Apple clang 21.0.0 |
| ブラウザ | Google Chrome 153.0.8010.53（CDP 9222） |
| network | `wrangler dev` 127.0.0.1:8787 と `turnserver` |
| 計測構成 | Debug（`-O0`）、in-process bench と実ブラウザ MediaTrack |

修正は `cmio_sink_publisher.mm` 1ファイルである。pool引数は `native_media.h` と `cv_nv12.mm`、Metalリンクは `macos/CMakeLists.txt` に足した。計測器は `video_pipeline_test.mm` の `--normbench` である。

`feedFrame` は全attachmentを取り除いてからBT.709の3キーだけを付け直す。書式表現の3キーと集合が一致するので、恒等パスの入力で起きていた失敗が消えた。キューに乗る全フレームが同じ書式を名乗るため、黒フレームと実フレームの書式は同じである。

#### S3-a 実映像（1280x720、恒等パス）

| 試行 | StartStream | `-12743` | published | consumer | hostログ |
|---|---|---|---|---|---|
| 1 | 成功 | 0件 | 2730 | 実映像 | `s3-a-host.txt` |
| 2 | status=-4 | 0件 | 2758 | 黒 | `s3-a2-host.txt` |
| 3 | 成功 | 0件 | 2745 | 実映像 | `s3-a3-host.txt` |

試行1と試行3はsample creationまで通り、`-12743` と `sample create failed` がともに0件である。decodeErr・normalizeErr・backpressure・needKeyframe・staleGen も0である。試行3は accepted=2746・published=2745、decodeUs avg=1198.4、toPublishUs avg=2174.0 である。

試行2は `StartStream` が status=-4 で失敗し続け、samples created=0 のまま黒だった。開始段階の失敗で、S1で特定したsample creationとは別の事象である。証跡は `s3-a2-host.txt` に残した。

consumer画像は `s3-a-consumer.png` と `s3-a3-consumer.png` に実映像、`s3-a2-consumer.png` に黒が写る。S3-aの証跡はhostログ3点、consumer画像3点、CDP出力3点である。

#### S3-b Normalize720p候補の比較（各600回）

| 入力 | 区分 | wallAvgUs | cpuUs | gpuAvgUs | copies |
|---|---|---|---|---|---|
| 1280x720 恒等 | CPU基準 | 0.53 | 0.64 | | 0 |
| 1280x720 恒等 | pool | 0.50 | 0.62 | | 0 |
| 960x540→720p | CPU基準 | 8756.81 | 8785.05 | | 1 |
| 960x540→720p | pool | 8574.61 | 8560.91 | | 1 |
| 960x540→720p | Metal | 494.90 | 299.71 | 64.48 | 1 |
| 1280x720 回転90 | CPU基準 | 3659.51 | 3680.51 | | 1 |
| 1280x720 回転90 | pool | 3496.95 | 3485.53 | | 1 |
| 1280x720 回転90 | Metal | 393.12 | 285.92 | 21.93 | 1 |

MetalはCPU基準よりletterboxで17.7倍、回転90度で9.3倍速い。GPU時間はletterboxで64.5µs、回転90度で21.9µsである。両経路とも出力はCPU基準とバイト単位で一致し、差分は0バイトである。

poolはallocationだけを差し替え、letterboxで2.1%、回転90度で4.4%の短縮にとどまる。変換ループのほうが大半を占める。identityはpoolを通っても入力をそのまま返し、copyは0本である。

入力のstrideは1280幅で1536、960幅で1024に作った。MetalとCPUの出力が一致したため、strideを読まない実装なら食い違うところまで確認できた。420f入力は `Expected video-range bi-planar 420v and quadrant rotation` という理由で拒否され、黙って変換されない。

pool枯渇は1枚を保持したまま補充を要求し、`refill=-6689` で拒否された。上限を超えて増えることはない。証跡は `work/records/s3-normbench.txt` と `work/records/s3-asan-normbench.txt` である。

採用は本単位の到達点でないため決めず、測定値と選択肢だけを残した。プロダクトのXcode構成がDebugのため、上表の値がそのまま実行時の値になる。

#### ゲート（最終tree）

| ゲート | 結果 | 証跡 |
|---|---|---|
| `sh scripts/test_macos_foundation.sh` | 10/10 pass、警告0 | `work/records/s3-gate-foundation.txt` |
| `sh scripts/build_macos_rtc.sh` | 12/12 pass、警告0 | `work/records/s3-gate-rtc.txt` |
| `cloud` `npm test` | 18/18 pass | `work/records/s3-gate-cloud.txt` |
| ASan/UBSan `ctest` | 10/10 pass、指摘0 | `work/records/s3-asan-gate.txt` |
| ASan `--normbench 120` | exit 0、指摘0、diffBytes=0 | `work/records/s3-asan-normbench.txt` |

`xcodebuild` は exit 0、`BUILD SUCCEEDED`、警告0で、証跡は `work/records/s3-xcodebuild.txt` である。ASanのCPU基準だけがletterboxで27968.8µsに伸び、Metalは500.4µsのままである。

### S4 幾何・色・時刻

| 項目 | 内容 |
|---|---|
| 段階・試験ID | 段階8 S4（向き・画角・色・PTS単調の実画像） |
| 目的・合格条件 | 送信した図形と同じ画角・向き・色を、consumer側PTSが下がらない状態で残す |
| 結果 | 成功（2入力とも11項目PASS、PTS違反0、3ゲート通過） |
| repo | `kmvirtualcamera-macos-coremediaio-foundation` |
| branch | `feature/macos-coremediaio-foundation` |
| HEAD | 製品コードの差分なし、本記録は次コミット |
| 未コミット差分 | 本記録と `work/`（未追跡）のみ |
| 日時 | 2026-09-27 19:47–20:15 JST |
| OS / CPU | macOS 26.7 (25G229) / Apple M3 Max (arm64) |
| Xcode・SDK / compiler | Xcode 26.6 (17F113) / macOS SDK 26.5 / Apple clang 21.0.0 |
| ブラウザ | Google Chrome 153.0.8010.53（CDP 9222） |
| network | `wrangler dev` 127.0.0.1:8787 と `turnserver` |
| 計測構成 | Debug（`-O0`）、CDP送信とAVFoundation consumer probe |

送信側は `s4-cdp.mjs`、consumer側は `s4-consumer.swift` である。送信はcanvas test doubleの大きさを切り替える。consumerは仮想カメラをAVFoundationで開き、PTSと画素を得る。

#### S4-a PTSと画素（各20秒）

| 試行 | 入力 | PTS frames | 違反 | deltaMs min/max/avg | consumerログ |
|---|---|---|---|---|---|
| A | 1280x720 | 600 | 0 | 33.33 / 33.34 / 33.33 | `s4-a-consumer.txt` |
| B | 1440x1080 | 598 | 0 | 33.33 / 66.67 / 33.39 | `s4-b-consumer.txt` |

PTSは両試行とも下がる区間が0で、平均は30fps相当である。試行Bの最大66.67は1フレーム分の間隔で、順序は保たれている。

試行Aは16:9入力なので帯がなく、四隅・左右中点・中心の画素は送信側グラデーションの対角投影と1以内で一致した。試行Bは4:3入力なので左右160pxの帯が入り、四隅と左右中点は40回すべて0、中心だけ128である。

赤マーカーのx範囲は試行A=40..158、試行B=186..266で、期待値40..160と187..267から6px以内に収まる。赤の値は(255,48,32)と(251,48,32)である。送信側 `#ff3020` がBT.709で往復した値に一致する。

証跡は `s4-a-consumer.txt`、`s4-b-consumer.txt`、PNG2点、送信ログ2点である。Chromeは `--use-fake-device-for-media-stream` で起動しており、カメラ一覧に仮想カメラが出ないためconsumerには使わなかった。

#### ゲート（現tree）

| ゲート | 結果 | 証跡 |
|---|---|---|
| `sh scripts/test_macos_foundation.sh` | 10/10 pass、警告0 | `work/records/s4-gate-foundation.txt` |
| `sh scripts/build_macos_rtc.sh` | 12/12 pass、警告0 | `work/records/s4-gate-rtc.txt` |
| `cloud` `npm test` | 18/18 pass | `work/records/s4-gate-cloud.txt` |

製品コードの差分はS4で0件である。3ゲートを本記録のtreeで再実行し、いずれも終了コード0になった。

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
