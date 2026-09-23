# macOS対応の実装基盤

`feature/macos-coremediaio-foundation` の入口です。これは完成したmacOS受信アプリではありません。
共通C++処理の抽出・DataChannel再構成の修正・macOSネイティブ部品・継続実装の資料を含みます。

最初に [現在の実装・検証状態](docs/macos/07_STATUS_AND_HANDOFF.md) を確認してください。
設計全体は [資料一覧](docs/macos/README.md)、次の実装指示は [Codex引き継ぎプロンプト](docs/macos/CODEX_CONTINUE_PROMPT.md) にあります。

```sh
sh scripts/test_foundation.sh
# 以下はmacOS実機上だけで実行。カメラの登録・署名はしません。
sh scripts/test_macos_foundation.sh
```

Windowsの既存ビルド入口 `windows/CMakeLists.txt` とクラウド／ブラウザ送信側は維持しています。
ただしWindowsのDataChannel再構成は共通の修正版を参照するため、動作変更があります。
Windowsアプリ全体の再ビルド・E2E試験を省略してmainへマージしないでください。
