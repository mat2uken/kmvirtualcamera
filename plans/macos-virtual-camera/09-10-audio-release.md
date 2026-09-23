# 段階9–10: 音声、配布、資料更新

映像の初回到達点は段階7、安定化の判定は段階8で行う。この文書は、音声を製品へ含める判断と、署名済みアプリの配布判断に必要な残作業を管理する。音声の追加有無、製品対応OS・CPU、配布方式は現時点で未決定である。

## 段階9: 音声

**判断条件**: 映像を先に公開するか、音声も同じ版に含めるかを製品要件として決める。WindowsのOpus復号・WASAPI実測と、Macの音声利用先を確認する。Camera Extensionだけでは仮想マイクを提供できない。

1. ブラウザOpusと `shared/receiver/audio/opus_rtp_decoder.*` をMacへ接続する。48kHz/stereo PCMの要素数とframe数、PLC、再接続時resetを実経路で確かめる。動画との時刻差と遅延を測る。
2. Core Audio出力が要件なら、専用audio workerから選択した出力deviceへbounded PCM queueを渡す。device変更、48kHz以外の実endpoint、underrun/overflow、ミュート、停止・再開を実音で確認する。RTC callbackを音声deviceの待機で止めない。
3. 仮想マイクが要件なら、使用するmacOSの音声device方式を別途調査・設計し、署名、導入、複数アプリからの利用、削除を映像Extensionとは別に検証する。方式が決まるまでCamera Extensionのsink/sourceを仮想マイクと呼ばない。

**完了条件**: 採用する音声範囲を記録する。採用する機能は、実ブラウザ入力からOS出力または仮想マイク利用アプリまで、音質・映像とのずれ・停止復帰を実測で示す。採用しない機能は公開仕様とUIに含めない。

## 段階10: 署名・更新・配布

**開始条件**: 段階8の品質判定が済み、配布対象OS・CPU・音声範囲・Team ID・bundle ID・App Group・配布経路を確定する。開発用署名での動作と、配布物の導入結果を分ける。

1. host、Camera Extension、埋め込むnative依存の署名とentitlementsを実際の生成物で確認する。hostとExtensionのTeam・識別子・App Groupを一致させ、不要な権限を除く。資格情報、署名秘密鍵、TURN credentialをrepo・ログ・配布物へ混入させない。
2. 配布経路に必要なnotarization、stapling、パッケージ化を実施する。新規Macの通常設定で `/Applications` 導入から一般アプリのcaptureまで再確認する。提出成功だけを実機合格にしない。
3. 更新、既存版からの置換、deactivation、アンインストール、再起動要求、残留したdeviceの有無を試す。hostが消えた後のExtension状態、再インストール時のstable IDと利用アプリの選択状態を記録する。
4. mbedTLS、libdatachannel、libopus、npm依存と推移的依存を、配布時点の脆弱性情報・ライセンス・実際の同梱版で点検する。`npm ci` 時点のauditには15件の警告があったため、個別に影響、更新可否、対処を記録する。依存の大型更新は対応範囲と回帰結果を分けて扱う。
5. 製品対象に合うbuild・試験をCIへ追加する。実行SHA、artifactハッシュ、署名結果、Mac導入映像、Windows回帰を対応付ける。CI成功だけで物理Macのcaptureを成功としない。
6. `docs/macos/03_MEDIA_AND_PROTOCOLS.md` のAU上限対策・timerに関する旧記述を直す。`05_IMPLEMENTATION_PLAN.md` の段階0–1、`macos/README.md` の試験状態、`07_STATUS_AND_HANDOFF.md` の実施記録も更新する。実際の最新SHAを根拠とし、公開手順に承認、失敗表示、更新、削除を含める。

**完了条件**: 配布する正確なartifactが、対象OS・CPUの通常設定で導入され、一般アプリで映像をcaptureできる。署名、notarization、更新、削除、依存点検、ライセンス、同梱内容、手順書を同じ版とartifactハッシュへ結び付けて記録する。未実施のOS・CPU・音声経路は対応対象へ含めない。

## 残る判断の担当と時期

段階4の署名前に開発用識別子を設定する担当を決める。段階8の測定後に映像の許容fps・遅延・負荷と対応OSを決める。段階9の開始前に音声の製品範囲を決める。段階10の配布作業前に配布経路と配布用署名の管理方法を決める。決定した値と根拠は[設計判断](../../docs/macos/08_DECISIONS_AND_REFERENCES.md)または後続の決定記録へ反映する。
