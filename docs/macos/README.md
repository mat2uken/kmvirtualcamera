# macOS移植資料

初回の設計に加えて、未修正監査項目R01–R13への対策をまとめました。
**開始点は [07 状態](07_STATUS_AND_HANDOFF.md) と [09 追加修正](09_REVIEW_FIXES.md) です。**

| 文書 | 内容 |
|---|---|
| [01](01_CODE_AUDIT.md) | 実コード監査、履歴と現時点の対応 |
| [02](02_ARCHITECTURE.md) | 共通C++/OS境界/プロセス/依存の設計 |
| [03](03_MEDIA_AND_PROTOCOLS.md) | 映像、wire、時刻、所有権 |
| [04](04_MACOS_INTEGRATION.md) | Camera Extensionとhostの実装手順 |
| [05](05_IMPLEMENTATION_PLAN.md) | 更新後の実装順序と完了条件 |
| [06](06_TEST_PLAN.md) | 共通、browser、Windows、Mac、TURN試験 |
| [07](07_STATUS_AND_HANDOFF.md) | 実装済み/未実装/実際の検証記録 |
| [08](08_DECISIONS_AND_REFERENCES.md) | 初回設計判断と一次資料 |
| [09](09_REVIEW_FIXES.md) | R01–R13対策、Opus依存、追加制約 |
| [継続prompt](CODEX_CONTINUE_PROMPT.md) | 次の担当者へ渡す指示 |

02/03/04/08の初回設計は保持しています。そこに書かれた実装状態・依存・優先度と新資料が異なる場合は、
07/09の現在の状態を優先してください。Camera Extension完成版ではありません。
