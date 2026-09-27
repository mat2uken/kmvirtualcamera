# macOS native foundation

このディレクトリはObjective-C++の実装部品です。host appとCamera Extensionを生成するが、署名はしない。
コンパイルとCTestは `scripts/test_macos_foundation.sh` で確認済みです（2026-09-28、12/12 pass）。
署名・導入・実機captureは未実施です。

## 入っているもの

| 部品 | 実装 | 現在の範囲 |
|---|---|---|
| CoreVideoバッファ | `receiver/native_media.h`, `receiver/cv_nv12.mm` | 参照管理、720p黒画面、420v正規化、CPU基準回転 |
| H.264 | `receiver/video_toolbox_decoder.mm` | Annex-B→length-prefix、SPS/PPS、bounded非同期デコード、世代つきcallback |
| Extension中継 | `camera-extension/frame_relay.*` | 認証済みproducerから受け取りsourceへ送る部品 |
| 有効化要求 | `host/extension_manager.*` | OSSystemExtensionRequestの送信・結果通知 |
| sink publisher | `receiver/cmio_sink_publisher.mm` | host→Extensionの映像供給、背圧、切断復帰 |
| 映像パイプライン | `receiver/video_pipeline.mm` | 非同期デコード、幾何/色、世代、統計 |
| 音声出力 | `receiver/audio_output.cpp`, `receiver/audio_pcm_queue.h` | bounded PCM queue、Core Audio出力、専用worker |
| メディアスモーク試験 | `tools/media_smoke.mm` | バッファ生成、単一AUデコード、stride/形式/色attachment観測、回転正規化、NV12 dump |
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

## 試験

`macos_audio_pcm_queue` は bounded PCM queue の契約（overflow/underrun/会計）を単体で確認する。
`macos_audio_output` は実endpointで 1kHz tone を再生し、mute・burst・gap・restart・device変更・44.1kHz を確認する。
いずれも `scripts/test_macos_foundation.sh` のCTestに含まれる。

## 未接続・制限

Provider/Device/StreamSourceの署名と導入、Xcodeプロジェクトの署名設定、HTTPSアダプター、仮想マイクは対象外・未実施です。
`Normalize720p` は **非恒等** のclean aperture、または1:1でないpixel aspect ratioのattachmentを拒否します。実デコーダ出力は1:1のPAR attachmentを持ちますが恒等判定を通過します（720pはidentity、非720pはletterbox変換を実測確認済み）。恒等clean apertureと非1:1 PARの実出力は未確認で、該当時は拒否します。

全体の設計・制約・次の作業は [macOS資料](../docs/macos/README.md) を参照してください。
