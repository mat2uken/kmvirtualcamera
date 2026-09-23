# macOS native foundation

このディレクトリはObjective-C++の実装部品です。現時点では `.app` や `.systemextension` を生成せず、カメラも登録しません。
コンパイルとCTестは `scripts/test_macos_foundation.sh` で確認済みです（2026-09-23、Apple Silicon / macOS 26.7 / Xcode 26.6 / SDK 26.5）。署名・導入・実機captureは未実施です。

## 入っているもの

| 部品 | 実装 | 現在の範囲 |
|---|---|---|
| CoreVideoバッファ | `receiver/native_media.h`, `receiver/cv_nv12.mm` | 参照管理、720p黒画面、420v正規化、CPU基準回転 |
| H.264 | `receiver/video_toolbox_decoder.mm` | Annex-B→length-prefix、SPS/PPS、VTデコード、HW使用状態取得 |
| Extension中継 | `camera-extension/frame_relay.*` | 認証済みproducerから受け取りsourceへ送る部品 |
| 有効化要求 | `host/extension_manager.*` | OSSystemExtensionRequestの送信・結果通知 |
| メディアスモーク試験 | `tools/media_smoke.mm` | バッファ生成、単一AUデコード、実出力のstride/形式/色attachment観測、4回転の正規化、NV12 dump |
| 設定例 | `config/*.entitlements.example` | Xcodeで識別子を確定するための例。完成した署名設定ではない |

## ビルド

リポジトリルートで `sh scripts/test_macos_foundation.sh` を実行します。
Xcode/SDKの組み合わせを記録し、SDK由来のコンパイルエラーは実際のヘッダーを根拠に修正してください。
CMakeとC++20対応環境が必要です。依存ライブラリの自動取得、管理者操作、インストールは行いません。

```sh
./build/macos-foundation/macos/km_macos_media_smoke
./build/macos-foundation/macos/km_macos_media_smoke --decode-au /path/to/one-access-unit.h264
```

後者の入力はSPS/PPSとIDRを含む**単一Access Unit**です。一般の長いH.264ファイルを切り出すツールではありません。
共通パーサーの単体試験に使う短い疑似NAL列は、デコーダ用の有効な映像fixtureではありません。
`--dump-normalized out.nv12` を付けると rotation 0 の正規化結果を密着NV12で書き出し、画像比較できます。

## 未接続・制限

Provider/Device/StreamSource、AppKitホスト、Xcodeプロジェクト、アプリ側sink publisher、producer認証、HTTPSアダプター、共通セッション統合は未実装です。
`VideoToolboxDecoder` はAUごとに非同期処理完了を待つ検証用の構成で、パイプライン化された本番デコーダではありません。
`Normalize720p` は **非恒等** のclean aperture、または1:1でないpixel aspect ratioのattachmentを拒否します。実デコーダ出力は1:1のPAR attachmentを持ちますが恒等判定を通過します（720pはidentity、非720pはletterbox変換を実測確認済み）。恒等clean apertureと非1:1 PARの実出力は未確認で、該当時は拒否します。

全体の設計・制約・次の作業は [macOS資料](../docs/macos/README.md) を参照してください。
