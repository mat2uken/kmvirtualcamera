# macOS対応と共通受信基盤

対象branch: `feature/macos-coremediaio-foundation`。
R01–R13追加修正の適用基準: `f7c7eb3777a00924e8498fb2ca1d0d277b1b5951`。

- [状態・実際の検証記録](docs/macos/07_STATUS_AND_HANDOFF.md)
- [R01–R13への追加修正](docs/macos/09_REVIEW_FIXES.md)
- [更新後の実装計画](docs/macos/05_IMPLEMENTATION_PLAN.md)
- [次の担当者へのプロンプト](docs/macos/CODEX_CONTINUE_PROMPT.md)

共通試験は `sh scripts/test_foundation.sh`、browser protocol試験は `sh scripts/test_browser_protocol.sh`。
Mac上の部品ビルドは `sh scripts/test_macos_foundation.sh`。このコマンドは仮想カメラの登録を行いません。
署名済みhost/Camera Extension、host-side sinkなどは継続実装が必要です。
