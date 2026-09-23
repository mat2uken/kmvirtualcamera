# Codex継続プロンプト（R01–R13追加修正版）

対象リポジトリ: mat2uken/kmvirtualcamera
対象branch: feature/macos-coremediaio-foundation
追加修正の適用基準: f7c7eb3777a00924e8498fb2ca1d0d277b1b5951

## 最初に読む

1. docs/macos/07_STATUS_AND_HANDOFF.md
2. docs/macos/09_REVIEW_FIXES.md
3. docs/macos/02_ARCHITECTURE.md と 03_MEDIA_AND_PROTOCOLS.md
4. docs/macos/04_MACOS_INTEGRATION.md と 05_IMPLEMENTATION_PLAN.md

今回はR01–R13への対策コードが存在する。旧資料の「未修正」をそのまま繰り返さない。
ただしWindows/macOS SDKや実RTCで検証済みではない。実際のコンパイラ/試験結果に基づいて修正すること。
パッケージ未適用の場合は同梱READMEに従い適用し、git diffを確認してから作業する。
mainへの直接commit、強制push、既存変更の削除をしない。

## 最初の実行

- 共通: sh scripts/test_foundation.sh
- Opus: 別build dirで -DKM_ENABLE_OPUS=ON を指定する。
- browser: npm ci --prefix cloud; sh scripts/test_browser_protocol.sh; npm run --prefix cloud build
- Windows: docs/macos/06_TEST_PLAN.mdの明示targetをbuildし、SDK/link/API差異を修正する。
- Mac: sh scripts/test_macos_foundation.sh。成功してもカメラ導入済みとは言わない。

## 守る契約

- 受信映像/音声やtoken/SDPを無条件でdisk/logへ保存しない。
- RTP遅着はdiscard方針。圧縮queue overflowは依存列を破棄しIDRを再要求する。
- Opus callbackのsize_tはinterleaved int16要素数。channelsを二重に掛けない。
- Decoder/COM操作はvideo owner。WASAPI/COM操作はaudio owner。
- Close/Initialize/破棄はcontrol thread。leased callbackから直接Closeしない。
- UI更新はpostし、main threadのjoinとworkerの同期SendMessageを循環待ちにしない。
- 時刻はRTP90kHz→unwrap→us、DC32bitus→unwrap。camera出力は別のlocal steady clock。
- デコード済みnative bufferを共通化のためだけに毎回CPU配列へコピーしない。
- 仮想カメラDLL/ExtensionへRTC/TLS/Opusをリンクしない。
- TURN TCP/TLSはlibnice buildだけ。既定libjuiceの制限を隠さない。

## 実装順序

1. SDK/依存での実ビルドとWindows回帰を成立させ、対策コードの配線を確認する。
2. Macの署名/host/Xcode/Provider/Device/sourceを追加し、固定720p30の生成映像をcaptureする。
3. host-side sink publisher、認証、sample所有権、consume通知、停止/reconnectを接続する。
4. 共通SessionClientとMacのNSURLSession adapter、RTC、VideoToolbox、previewを接続する。
5. 非同期decode/geometry/color/負荷/長時間を検証する。

stubが成功を返す実装や、ログだけでテストに見せかける実装は禁止。
利用できないhardware/署名がある場合は、実装済み・未実装・未試験を分けて報告する。
各段階で実行command、環境、結果、変更先、残課題をdocsに追記する。
