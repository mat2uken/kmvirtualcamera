# 各段階の確認記録と受け入れ判定

実装担当者は各段階の結果を、その段階を実行したSHAに結び付けて残す。結果の欄は「成功」「失敗」「未実施」「対象外」を使い、buildだけで実機captureを成功にしない。環境に試験対象がなければ未実施と理由を記す。この文書は記録の様式であり、現在の試験成功を追加で主張するものではない。

## 1件ごとの記録項目

```text
段階・試験ID:
目的・合格条件:
結果: 成功 / 失敗 / 未実施 / 対象外
repo / branch / HEAD / 未コミット差分:
artifact名 / SHA256 / 署名識別子:
日時 / OS / version / CPU / Xcode・SDK / compiler:
Windows SDK / ブラウザ・版 / 受信利用アプリ・版:
network構成 / ICE candidate種別 / TURN backend:
実行コマンドまたは操作順 / 終了コード:
期待した映像・音・状態 / 実際の観測:
fps / 遅延 / queue深さ / drop / CPU・GPU / memory:
ログ・動画・画像の保存先:
失敗時の再現条件 / 修正SHA / 再試験結果:
```

token、SDP内の認証情報、秘密鍵、証明書の秘密部分は添付しない。大量ログは `work/` に原本を置き、結果欄に終了コードと失敗箇所・集計・絶対パスを書く。動画やスクリーンショットはsourceとconsumerが分かる名前にする。

## 段階ごとの必須証拠

| 段階 | 最低限の結果 |
|---|---|
| 2 | Windows製品build、限定CTest、CI 3 job |
| 3 | RTC 2経路、音声、Close競合、UDP TURN |
| 4 | Mac部品・実AU decode・Xcode build |
| 5 | 署名、導入、列挙、生成映像capture |
| 6 | host生成映像、認証、queue、sink→source |
| 7 | 実ブラウザ2経路、断線復旧、Mac capture |
| 8 | 色・回転・時刻、30分、複数consumer |
| 9 | 採用する音声経路の実音と遅延 |
| 10 | 配布物の署名・導入・更新・削除 |

段階2のCIはremoteブランチへ公開されたSHAの結果を使う。段階4の `sh scripts/test_macos_foundation.sh` は署名・導入を含まない。段階5以降のMac判定は実際のCamera Extensionの導入状態を含む。段階3のWindows CTest全件は登録やcaptureの副作用を確認してから個別に選ぶ。[従来の試験計画](../../docs/macos/06_TEST_PLAN.md)も参照する。

## 映像経路の切り分け

接続失敗時は、session作成、Offer/Answer、ICE/DTLS、MediaTrackまたはDCを順に確認する。続いてAU生成、decode、420v正規化、publisher投入、Extension消費、source送出、consumer captureを調べる。各地点の受信数と最後の世代・時刻を記録する。黒画面なら、未接続の待機画面、decode失敗、publisher切断、source停止、consumer側停止を区別する。

## 受け入れの判定順

1. 単体・共通試験の終了コードと対象SHAを確認する。
2. nativeターゲットのbuildと署名物の生成を確認する。
3. Extensionの承認、device列挙、format公開を確認する。
4. host生成映像、実ブラウザ映像の順にconsumer captureを確認する。
5. 断線・停止・再接続、複数consumer、長時間の記録を確認する。
6. 配布判定では実際のartifactハッシュと導入されたbundleを照合する。

ある段階に失敗・未実施が残る場合、次段階へ進めるかを影響箇所に照らして記録する。例としてWindows音声の問題はMacの生成映像試験を止めないが、RTC共通部のビルド不良は段階7の完了を止める。許容値未決定の性能項目を「成功」にせず、測定結果と決定待ちを残す。
