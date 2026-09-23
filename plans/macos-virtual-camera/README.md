# macOS仮想カメラの残作業計画

作成日: 2026-09-23。対象は `feature/macos-coremediaio-foundation` の `fdf5cab939c73f5fdaea931f6094d72c4b8a11df` 以降。映像をカメラとして使える状態から配布判断まで、実装担当者が作業と結果を追うための計画である。

## 現在地と最初の到達点

既存計画の段階0と1は完了した。ZIPのR01–R13を適用し、共通CTest、Opus有効構成、ASan/UBSan、ブラウザの型検査・protocol試験・Vite buildをApple Silicon Macで通した。4コミットはローカルにあり、featureブランチのremoteより4コミット先にある。CI、Windowsの製品ビルド、Macの署名・Camera Extension導入、実ブラウザとの接続は未確認である。最新の実施記録は[状態と引き継ぎ](../../docs/macos/07_STATUS_AND_HANDOFF.md)を参照する。

**最初の到達点**は、通常設定のMacでブラウザ映像を仮想カメラとして表示すること。720p30／420vで一般アプリが取得し、停止・再接続後に古い映像を残さない。生成映像とhost側sink投入を先に個別に通す。仮想マイク、Intel対応、配布用notarizationは後段で判定する。

## 実施順

| 段階 | 作業 | 依存 | 計画 |
|---|---|---|---|
| 2 | Windows SDKビルドとCI | 段階1 | [Windows確認](02-03-windows-native.md) |
| 3 | Windows RTC・音声・TURN実動作 | 段階2 | [Windows確認](02-03-windows-native.md) |
| 4 | Mac部品の実SDK確認とhostの土台 | 段階1 | [Mac source](04-05-macos-source.md) |
| 5 | 署名済みExtensionの生成映像 | 段階4 | [Mac source](04-05-macos-source.md) |
| 6 | sink、producer認証、host投入 | 段階5 | [sink投入](06-sink-publisher.md) |
| 7 | 共通受信とブラウザ映像の接続 | 段階3・6 | [映像接続](07-live-receiver.md) |
| 8 | 画質・遅延・寿命・負荷の検証 | 段階7 | [安定化](08-stability.md) |
| 9 | 音声出力と仮想マイクの判断 | 段階7 | [音声と配布](09-10-audio-release.md) |
| 10 | 配布、更新、資料の整合 | 段階8 | [音声と配布](09-10-audio-release.md) |

段階2–3はWindows既存機能の退行と共通RTCの実依存を確認するために先行する。段階4–6のMac作業は段階2–3と独立して進められる。段階9の仮想マイクは映像の初回到達点には含めない。段階10の配布判断は必要な製品対象と署名体制が決まってから行う。

## 実施時の共通ルール

1. 各段階の開始時に対象repo、branch、HEAD、既存変更を記録し、対象外の変更を保持する。段階を終える際は差分と依存の追加を確認する。
2. 実SDKヘッダーと実行結果をAPI名・可用性の根拠にする。既存文書の記述とコードが違う場合はコードを調べ、差を記録する。
3. 受信アプリにRTC・TLS・VideoToolboxを置く。Extensionにはdevice/stream、producer確認、最新映像、30fps送出、待機画面だけを置く。OS固有の画素バッファを共通C++公開型へコピーしない。
4. 作業単位ごとに「ビルド」「インストール」「列挙」「映像」「長時間」「配布」を別の確認欄として扱う。前段の成功を後段の証拠にしない。[記録様式](verification.md)を使う。
5. 新しいprotocolや大型依存の採用、音声・Intel・製品対応OSの拡大が必要になれば、既存の[設計判断](../../docs/macos/08_DECISIONS_AND_REFERENCES.md)と実測を照合してから判断を記録する。

## 判断待ちと作業条件

署名に使うTeam ID、bundle ID、App Group、配布方式、対象macOS版、Intelの要否は、この計画作成時点で製品値として確定していない。開発用の仮値を製品設定として固定しない。通常設定のMacでExtensionを承認できる環境、Windows SDK端末、実ブラウザ端末、UDP TURN環境を、それぞれ該当段階の実動作確認前に用意する。秘密鍵・トークン・証明書の中身をログやrepoへ保存しない。

## 参照資料

- [Mac先行実行計画](00-macos-first-plan.md)（段階4→5→6を先行し、段階7のWindows依存を分離した具体的な作業順）
- [従来の段階計画](../../docs/macos/05_IMPLEMENTATION_PLAN.md)
- [macOS統合の実装箇所](../../docs/macos/04_MACOS_INTEGRATION.md)
- [試験計画](../../docs/macos/06_TEST_PLAN.md)
- [R01–R13の対策と未検証事項](../../docs/macos/09_REVIEW_FIXES.md)
- [構成と共通処理](../../docs/macos/02_ARCHITECTURE.md)

既存の[メディア資料](../../docs/macos/03_MEDIA_AND_PROTOCOLS.md)には、適用後のコードと異なり、ブラウザAU上限対策やWindows timer配線を未実装とする旧記述が残る。段階10で修正する。それまでは該当コードとR01–R13の対策記録を照合する。
