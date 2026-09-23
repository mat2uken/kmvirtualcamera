# 段階8: 映像品質、遅延、寿命の安定化

段階7の両映像経路を動かし、同じSHAと環境で問題を測る。対象はMacのdecoder、正規化、video worker、host publisher、frame relay、ブラウザ送信部。[既存の試験計画](../../docs/macos/06_TEST_PLAN.md)も参照する。

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
