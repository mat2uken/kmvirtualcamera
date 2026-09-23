# KM Virtual Camera macOS移植資料

作成日: 2026-09-23。対象ブランチ: `feature/macos-coremediaio-foundation`。
調査基準: `main` の `1f22e7423748a1de6469e40ef5516a29d9b19fa8`。
目的は、Windows版を保守しながらC++20の通信・メディア処理を共有し、Apple標準APIでmacOSカメラを追加することです。

**本ブランチは実装の土台です。macOSでブラウザ映像を仮想カメラへ出力できる完成状態ではありません。**
共通コードのLinux試験と、macOS SDKコンパイル・署名・E2E試験を区別します。

| 資料 | 内容 |
|---|---|
| [01 コード監査](01_CODE_AUDIT.md) | README外の実装、修正済み問題、残存課題と根拠箇所 |
| [02 アーキテクチャ](02_ARCHITECTURE.md) | 共通化境界、プロセス、依存、現段階と完成形 |
| [03 メディア／プロトコル](03_MEDIA_AND_PROTOCOLS.md) | wire仕様、H.264、キュー、時刻、色、所有権 |
| [04 macOS統合](04_MACOS_INTEGRATION.md) | Provider/Device/Streams、sink publisher、署名、有効化 |
| [05 実装計画](05_IMPLEMENTATION_PLAN.md) | 順序、変更ファイル、完了条件、回帰防止 |
| [06 テスト計画](06_TEST_PLAN.md) | 共通・Windows・Mac・ブラウザ・長時間試験 |
| [07 現在の状態と引き継ぎ](07_STATUS_AND_HANDOFF.md) | 実施済み／未検証／未実装、既知の制限 |
| [08 決定事項と参考情報](08_DECISIONS_AND_REFERENCES.md) | 採否の理由、一次資料、見直し条件 |
| [Codex継続プロンプト](CODEX_CONTINUE_PROMPT.md) | 次の担当者への自己完結した実装指示 |

読む順序は07→01→02→04→05です。試験時は06、実装上の契約確認は03を使用します。
ルートの既存READMEと既存計画はWindows PoCの説明です。現行動作の根拠はコード、macOS基盤の完了範囲は07を優先します。
