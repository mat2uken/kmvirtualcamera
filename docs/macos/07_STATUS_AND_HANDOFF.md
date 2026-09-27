# 07 状態と引き継ぎ

作成日: 2026-09-23。最終更新: 2026-09-28。適用基準は `47ce46e`（段階9 A6の記録コミット）。
macOS仮想カメラの実装土台。段階0–9が完了し、段階10（署名・配布）が未着手である。

## 実施済み

| 段階 | 内容 | 結果 |
|---|---|---|
| 0–1 | R01–R13対策の適用と共通試験 | CTest全通過 |
| 2–3 | Windows Receiver/RTC・WASAPI・TURN | 実測済み（段階3） |
| 4–5 | Mac native componentsのビルド | VideoToolbox decoder・CoreVideo buffer |
| 6–8 | sink publisher・host・RTC接続・映像安定化 | 実ブラウザの映像受信・30分連続 |
| 9 | 音声（Opus復号・Core Audio出力） | 実ブラウザの音声受信・実endpoint出力 |

## Macでの実測

| 項目 | 値 | 証跡 |
|---|---|---|
| foundation CTest | 12/12 pass、警告0 | `work/records/09c-gate-foundation.txt` |
| rtc CTest | 15/15 pass、警告0 | `work/records/09c-gate-rtc.txt` |
| cloud npm test | 18/18 pass | `work/records/09c-gate-cloud.txt` |
| xcodebuild Debug | BUILD SUCCEEDED、警告0 | `work/records/09c-gate-xcodebuild.txt` |
| 映像受信 | 720p 30fps、30分連続 | [08-stability](08-stability.md) |
| 音声受信 | Opus 48kHz stereo、mean 26.8ms | [09-10-audio-release](09-10-audio-release.md) |
| 音声出力 | 定常 underrun=0、rate比0.9997 | 同上 |

## 未検証

- 署名、notarization、stapling、パッケージ化（段階10）
- 通常設定のMacでの導入と、一般アプリからの実camera capture（B1–B4）
- 実マイク（WASAPI）・Windows実カメラ・端末QR走査・配布前目視
- 仮想マイク（C4で対象外）

## 依然として未実装のmacOS機能

署名済みhost app、Camera ExtensionのProvider/Device/StreamSource、配布物の作成と導入である。
host・sink publisher・VideoToolbox受信パイプライン・Core Audio出力は実装・実測済みだが、
署名と導入を伴わない。仮想マイクはC4で対象外とした。

## 次の担当者

段階10の署名・配布へ進む。対象OS・CPU・配布方式・Team ID・bundle ID・App Groupを確定し、
開発用署名での動作と配布物の導入結果を分けて記録する。
資格情報、署名秘密鍵、TURN credentialをrepo・ログ・配布物へ混入させない。
