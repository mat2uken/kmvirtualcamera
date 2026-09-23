# 03 メディア・プロトコル契約

## ブラウザとの互換

新しいmacOS専用ネットワークプロトコルは作りません。
通常のWebRTC MediaTrack H.264と、既存の `km-video-stream` / `km-control` DataChannelを引き継ぎます。
Cloudflareのセッション／Offer／Answer APIを変えず、client表示名等だけをplatform情報として扱います。

DataChannelの現行12-byteヘッダーはlittle-endianです。

| byte | 型 | 意味 |
|---|---|---|
| 0..1 | u16 | magic 0x4B4D |
| 2 | u8 | key=1、config=2、first=4、last=8 |
| 3 | u8 | payload type: H.264=1、Opus=2（今回のvideo parserは1だけ） |
| 4 | u8 | chunk index |
| 5 | u8 | total chunks、1..255 |
| 6..7 | u16 | frame sequence |
| 8..11 | u32 | sender timestamp microseconds |

現ブラウザのpayload上限1180、アプリケーションpacket長1192です。
これはSCTP／DTLS／IP等のヘッダーを加えたUDPデータグラム長でも、IP非断片化の保証でもありません。
現wireの1AU最大は255×1180=300900bytes。共通H.264 helperの4MiB上限とは別の制限です。
送信側はこのサイズ超過時にAU全体を送らず、bitrate／解像度を見直してIDR復旧するガードが未追加です。

同じframeSeqのchunk数・timestamp・固定flagsは一致させます。重複chunkは同一内容だけを許可します。
受信側は32pending frame／2MiB payload上限。管理構造・出力vector・SPS/PPS cacheも別のメモリを使うため、2MiBをプロセス総使用量とは表現しません。
reorder待ち10ms、保持期限60ms、PLI間隔50msは今回の基準値です。ネットワーク条件ごとの性能保証ではありません。
独立timerから `OnTimerTick` を20ms程度で呼ぶ配線を次段階で追加し、無通信時にも掃除・復旧させます。

## H.264とデコーダ

共通側の標準入力はAnnex-BのAccess Unitです。1callback＝1AUが成立するかをRTP/DC双方で試験します。
NAL spanは元入力を借用するため、非同期に渡す前に入力全体の所有権を確保します。
Annex-B / length-prefixの判定は厳格に行い、不完全なバッファを推測修復しません。

受信key flagだけで復旧せず、IDR NALとSPS/PPSを確認します。
現在の小さな実装はSPS/PPSを1対として管理します。複数parameter-set IDや部分更新を一般的に処理する完全なH.264状態機械ではありません。
カメラ切替でSPSだけ変化した際に古いPPSを組み合わせないことを優先し、部分ペアを拒否します。
実ブラウザのfixtureで同じAUに必要ペアが存在するか確認し、必要ならID参照を解釈するキャッシュへ拡張します。

VideoToolbox経路はSPS/PPS→CMVideoFormatDescription、4-byte NAL-length付きpayload→CMSampleBuffer→VTDecompressionSessionです。
hardware decodeは有効化希望であり、必須指定ではありません。`hardwareActive()`で実際の使用状態を報告します。
現在の実装はAUごとにwaitするスモーク用です。本番はbounded outstanding decodeとgenerationつきcallbackへ移行します。
無期限に大量submitしないこと、stop時のcallback寿命、format変更時のdrain/invalidateが必須です。

## 時刻を3種類に分離

1. 送信メディア時刻: RTP90kHzかDataChannelのmicroseconds。
2. 受信ローカル時刻: 到着・デコード完了のmonotonic time。
3. カメラ出力時刻: Mac host clockによるPTSとhostTimeInNanoseconds。

`TimestampUnwrapper32` は32bitの連続差分を64bitへ伸長します。同一streamで隣接差が2^31未満という前提があります。
DataChannelの32bit microsecondsは約71.58分でwrapします。RTPとDCでunwrapperを共有しません。
送信元変更、SSRC変更、再接続、重大な時刻discontinuityで世代を変え、対応関係を作り直します。
`TicksToMicroseconds` は単位だけを変えるもので、ブラウザとMacの時計を同期する処理ではありません。

`RationalPacer` は絶対締切を有理数で作り、30fpsを33msや16666usの累積にしません。
遅れたtimerが復帰したとき、過去フレームを一気に全送信せず過去の締切を捨てます。
output durationは1/30、PTSは同じhost clockに揃え、時刻が前へ進むことを確認します。
現在のWindows count×16666を置き換える配線はまだありません。

## キューと所有権

| 場所 | 契約 |
|---|---|
| パケット→圧縮AU | bounded、期限・欠損を認識、未知入力をreject |
| 圧縮AU→decode | 参照依存を維持。満杯時は依存列を無造作に間引かずIDR復旧 |
| decode済み→camera | immutable最新フレーム優先、古い未表示フレームを蓄積しない |
| preview | camera出力を待たせない。非表示時は表示処理だけ停止 |
| App→sink queue | 有限queue、所有権と消費通知を明示。RTC threadをblockしない |
| Extension | producer世代、retainした最新フレーム、短いtimeout後に黒画面 |

`LatestFrame<T>` はdecoded frame専用です。mutexで短い参照交換を行い、早期の独自lock-free実装を避けています。
`PixelBuffer` はCoreVideo retain/releaseを管理します。callback外へ持ち出す際は明示的retainが必要です。
CoreMediaの単純queueはC++スマートポインターのように所有権を管理しません。後続publisherでenqueue成功／失敗／stop drainごとのretain/releaseをテストしてください。

## ピクセル形式と画面の意味

公開形式の初期設計は1280×720、30fps、Apple420v（video-range bi-planar4:2:0）です。
WindowsのNV12 FourCC値をAppleの値として送らず、意味から対応付けます。
Y/UV各planeのstrideと有効bytesを持ち、widthとstrideが同じだと仮定しません。
拡大縮小・回転はcrop、clean aperture、pixel aspect、色range/matrixと別の概念です。

共通CPU基準実装はnearest neighbor、四方向回転、even座標のletterboxです。高品質補間や色変換は未実装です。
Mac helperはgeometry attachmentを拒否する保守的実装です。恒等clean apertureまで拒否し得るため実映像対応前に改善します。
色metadataのコピーだけで601→709やfull→videoの画素変換をしたことにはなりません。
固定709へ統一する場合は、入力の色指定を読んで必要な画素変換を追加します。

完全なゼロコピーは未測定です。まず不要なCPU読み戻しを減らし、CVPixelBufferPoolとVideoToolbox転送または必要最小限Metal処理を追加します。
現CPUフォールバックを残して、画角・色・回転結果の基準として比較します。
