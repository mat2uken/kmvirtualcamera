# 07 状態と引き継ぎ（R01–R13追加修正版）

作成日: 2026-09-23。適用基準は `f7c7eb3777a00924e8498fb2ca1d0d277b1b5951`。
macOSアプリ完成版ではなく、共通受信とWindows側の問題修正を追加した実装土台です。

## 今回の実装

R01–R13の対策コードを追加しました。詳細は [09_REVIEW_FIXES.md](09_REVIEW_FIXES.md)。
パケット検証、JSON/HTTP処理、Opus復号、bounded queueを変更しました。
decoder操作の単一owner、pacing、timer、callback終了待ちも追加しました。
ブラウザではAU上限と旧世代の結果の破棄を実装しました。

## パッケージ作成時の試験

Linuxの作業環境で、下記のソースを直接コンパイル/実行しました。
新しいテストを実行したことと、完全なリポジトリのCMakeビルドが成功したことを混同しないでください。

| 試験 | 結果 |
|---|---|
| `test_review.cpp` + H264 RTP reassembler / Clang / ASan+UBSan | Passed。固定seedの不正datagram 50,000件を含む |
| 同上 / GCC / Release | Passed |
| `test_session.cpp` / Clang / ASan+UBSan | Passed。mock HTTP/JSON/ICE/escape/応答上限 |
| `test_opus.cpp` + OpusRtpDecoder / Clang / ASan+UBSan | Passed。Linuxにインストール済みの実libopus.so.0へリンク |
| browser sender/helper / `tsc --strict` | Passed |
| `tests/browser/test_packetizer.cjs` / Node 22 | Passed。255境界、oversize send0回、失敗、IDR/世代 |

パッケージ作成時のOpus試験はLinuxのシステムライブラリを使いました。
GitHub Actions workflowを同梱していますが、CI成功は確認していません。

## Macでの適用と再確認

Apple Silicon Mac、Xcode 26.6、macOS SDK 26.5で、基準コミット`f7c7eb3`へ修正を適用しました。
`sh scripts/test_foundation.sh`はCTest 4/4、Opus有効構成は5/5、ASan/UBSan構成は4/4で成功しました。
Opus有効構成では、CMakeが取得したlibopus 1.6.1をビルドしています。

`npm ci --prefix cloud`、`sh scripts/test_browser_protocol.sh`、`npm run --prefix cloud build`も成功しました。
最初の型検査でWebCodecsの`SharedArrayBuffer`型を扱えない箇所を検出したため、
設定情報の読み取りを修正し、SPS/PPSを取得できる回帰試験を追加して再実行しました。
ソース、共通試験、ブラウザ試験の結果であり、OSへのカメラ登録や実映像の確認ではありません。

## 未検証

WindowsではSDK全体ビルド、WinHTTPの実TLSとキャンセル、WASAPI出力、仮想カメラE2Eを未確認です。
libdatachannelをリンクしたRTC経路はMacでビルドとオフライン設定試験まで確認し、接続・TURN relay・実ブラウザの送信は未確認です。
MacではObjective-C++部品のSDKビルド、署名、実機動作を未確認です。
今回の修正でMacの仮想カメラがインストールできるようになったわけではありません。

## 依然として未実装のmacOS機能

署名済みhost app、Xcodeプロジェクト、Provider/Device/StreamSource、host-side sink publisherは未実装です。
ReceiverEngine/UI統合の接続と仮想マイクは未実装です（共通ReceiverEngine部・シグナリング専用ワーカー・NSURLSession adapter・producer認証は各単位で試験済み、AppKit join UI・QRとローカルsignalingでのセッション作成・Offer受信・RTC未実装の失敗表示も確認済み、libdatachannel導入とオフライン試験は単位6で確認、RTC配線は残り）。
VideoToolbox部品は初回のAU単位wait構成であり、bounded非同期パイプラインへの拡張が必要です。

## 次の担当者

[CODEX_CONTINUE_PROMPT.md](CODEX_CONTINUE_PROMPT.md) を開始点とし、まずWindows/native依存の
コンパイルエラーを解消・記録してください。その後、Mac部品のビルド → 署名した生成映像カメラ →
host→sink→source → RTC受信接続と進めます。
対策コードを「全試験に通過した既存仕様」と見なして追加修正を避けないでください。
