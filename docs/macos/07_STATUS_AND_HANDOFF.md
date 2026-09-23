# 07 現在の状態と引き継ぎ

作成日: 2026-09-23。
ブランチ: `feature/macos-coremediaio-foundation`。
基準main: `1f22e7423748a1de6469e40ef5516a29d9b19fa8`。
共通コードの初回コミット: `22380cd7e9cbe88bf5b1eedf3c47f07f7828b094`。

**macOS移植完了ではありません。Mac実機で次の実装を続けるための、コードと設計資料の土台です。**

## 追加・変更したもの

| 区分 | 内容 | 状態 |
|---|---|---|
| 共通media | DataChannel再構成をbounded/validatedへ変更、既定dump廃止 | コード追加・Linux単体検証済み |
| 共通helpers | H.264変換、clock unwrap、有理数pacing、latest decoded frame、NV12基準処理 | コード追加・Linux単体検証済み |
| RTC抽出 | RTP、TWCC、RTCP、帯域推定、signaling modelをsharedへ移動 | 既存blobと同内容。追加の安全性修正／全体ビルドは未実施 |
| Windows互換 | 旧パスをsharedへの転送ファイル化 | DCの直接compile試験済み。Windows全体は未検証 |
| 共通境界 | EncodedVideoFrame / IVideoPipeline / IHttpTransport | 宣言・契約。全adapterの接続は未実装 |
| macOS codec | VideoToolboxDecoder、PixelBuffer、420v正規化 | 実装コードあり。SDKコンパイル・実機は未検証 |
| macOS camera | KMFrameRelay、KMExtensionManager | 実装部品あり。Provider/Device/Streamsとホストアプリは未実装 |
| build | root CMake、MacライブラリとCLI、shell試験入口 | 共通CMake検証済み。Mac CMake実行は未実施 |
| 資料 | 監査、設計、契約、統合、計画、試験、決定、継続prompt | 本ディレクトリ |

## 実際に実行した試験

本タスクのLinux環境で以下の実行結果を確認しました。GitHub Actionsを実行したという記録ではありません。

```text
GCC 14.2.0 / Release:
  common_contracts        Passed
  datachannel_regression  Passed
  100% tests passed, 0 tests failed out of 2

Clang 17.0.0 / Debug / AddressSanitizer + UndefinedBehaviorSanitizer:
  common_contracts        Passed
  datachannel_regression  Passed
  100% tests passed, 0 tests failed out of 2

GCC direct compile of windows/receiver/codec/dc_video_depacketizer.cpp
  + tests/shared/test_dc.cpp:
  datachannel_regression: all checks passed, malformed corpus=20000
```

MacのメディアCLIはテスト入口を作成しただけで、成功ログはありません。
WindowsのCOM登録、仮想カメラcapture、WebRTC通信、クラウド／ブラウザE2Eも今回実行していません。

## まだ存在しないもの

署名済みMacアプリ、Xcodeプロジェクト、Provider/Device/StreamSource、アプリ側sink publisher、producer認証、
NSURLSession adapter、完全な共通ReceiverEngine、Macプレビュー/UI統合、音声デコード・仮想マイクは未実装です。
現状のCLIを起動してもシステムにカメラは追加されません。

## 残る制限・重要な作業

- 監査R01..R04: RTP/RTCPの長さ検証、音声の圧縮データ→PCM誤扱いは未修正です。本番／main統合前に解消してください。
- macOSの正規化はgeometry attachmentを一律拒否します。恒等clean aperture/SARも含め、実映像で確認して改善が必要です。
- VideoToolboxDecoderはAUごとにwaitする検証用構成です。非同期パイプライン・bounded outstanding・stop世代は追加が必要です。
- relayのproducer検証、source参照数、timer駆動、消費通知の完全なflow controlは外部owner側の実装と試験が必要です。
- 時刻helperを追加しても、既存のRTP90kHz／DC microseconds／count×16666の混在はまだ置換されていません。
- 音声やTURNを含む従来READMEの機能説明を、今回のコードの検証済み機能と読み替えないでください。

## 次の担当者が最初に行うこと

1. [CODEX_CONTINUE_PROMPT](CODEX_CONTINUE_PROMPT.md) と [コード監査](01_CODE_AUDIT.md) を読む。
2. 共通テストを再実行し、Macで `sh scripts/test_macos_foundation.sh` を実行する。
3. SDKの実エラーを修正し、生成映像のsigned Camera Extensionを先に成立させる。
4. 次にhost→sink→sourceを通し、その後共通RTC/sessionと接続する。

この順序と具体的な変更先・完了条件は [05](05_IMPLEMENTATION_PLAN.md) にあります。
