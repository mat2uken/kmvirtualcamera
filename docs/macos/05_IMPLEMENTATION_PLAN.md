# 05 継続実装計画

## 実装単位と順序

各作業は小さなコミットに分け、既存Windows受信を壊していないことを段階的に確認します。
「設計した」「コードを書いた」「ビルドできた」「OSに登録できた」「E2Eが動いた」を別の完了状態にします。

| ID | 作業と主ファイル | 完了条件 |
|---|---|---|
| M01 | `macos/*` SDKビルド、media smoke、clean aperture恒等判定 | 実SDKコンパイル、黒画面、実AUデコード成功。OS/SDK/arch記録 |
| M02 | Xcode host/extension、provider/device/source、有効化UI | 通常設定のMacで導入・承認・列挙・生成映像capture成功 |
| M03 | sink source、`cmio_sink_publisher.mm`、producer認証、relay接続 | ホスト生成映像が別プロセスのsourceから取得できる。不正producer拒否 |
| W01 | 監査R01..R04のRTP/RTCP/音声安全性修正 | 各不正入力回帰試験とsanitizer、Windows全体ビルド。音声未対応なら明示無効化 |
| C01 | `ReceiverEngine` とsignaling状態機械、HTTP分離 | GUIなしでテスト可能。HTTP失敗・cancel・reconnect・終了をfake transportで試験 |
| C02 | `PeerConnectionManager`抽出、callback世代／時刻／TURN／DC timer | OSヘッダーなし。RTP/DCの型契約、TURNを含む実接続、停止時の寿命試験 |
| W02 | Windows AppControllerをadapter化、decoder単一owner | 既存UI・登録・RTP/DC映像が回帰しない。キュー満杯を無視しない |
| M04 | NSURLSession、共通engine、VideoToolbox、publisher接続 | 既存Safari/Chrome senderからMacカメラまで720p30映像 |
| M05 | preview/QR/操作、色・回転・切断・複数consumer | ユーザー操作とcapture継続を両立し、プレビュー停止で映像出力が止まらない |
| P01 | CVPixelBufferPool、非同期VT、必要最小のGPU正規化 | CPU基準と画角・色が一致し、コピー数／RSS／遅延を実測。性能の根拠を保存 |
| A01 | 正しいOpus復号、音声ジッタ、Core Audio出力 | 音声サンプル数が正しく、無音・切断・同期試験合格。仮想マイクは別途判断 |
| D01 | 署名・notarization・update/uninstall・対応OS/arch | クリーン環境で導入→更新→削除の確認。必要な実測・QA記録あり |

最初の経路はM01→M02→M03です。その後C01/C02/M04へ接続します。
W01の安全性修正はM02/M03と独立して進められますが、本番利用・mainへの統合前の必須条件です。
独自XPC/共有メモリ、Metal処理の大規模追加、仮想マイクdriverは初回成立の前提にしません。

## 共通セッションの具体的な分割

`AppController::StartNewSignalingSession` / `SignalingWorkerProc` からUI操作を取り除き、状態とイベントだけを発行します。
`WinHttpClient::Request` をWindows transportへ残し、CreateSession/PollOffer/PutAnswerを共通SignalingClientへ移します。
JSONはschemaに沿ってparseし、未知field許容・必須field検査・文字列escape・応答上限をテストします。
小さく保つことと不完全な自作JSONを維持することを混同しません。必要なら小規模ライブラリを1つ評価し根拠を残します。

PeerConnectionManagerが受けたAUは `EncodedVideoFrame` へ所有権つきで変換します。
transportごとのtimestamp domain、受信monotonic time、generationを付けます。
codecへの初期化・投入・resetをvideo ownerだけが行い、切断時は新世代の仕事と混ざらないようにします。
UI通知はmain executorへ転送し、任意のRTC threadからネイティブUIを直接更新しません。

## Windows回帰の抑え方

現在の転送 `.cpp` は過渡的な互換策です。共通ライブラリへ切替える際はCMakeの旧source entryを同時に外します。
Windowsバッファ／DLL IPC／COM登録を同じコミットで作り直さず、まずinterface越しに元実装を呼びます。
既存tests/scriptsが旧ファイルパスを直接コンパイルする箇所を検索し、転送ファイル撤去と一緒に更新します。
Windows rootの設定を共通rootへ無条件で持ち込まず、MSVCオプション・Win32 define・リンクをtarget単位にします。

## 完了管理

各コミットに目的、対象ファイル、動作変更、実行したコマンド、結果、未確認環境を記録します。
SDKや実機がない場合はチェック項目をskip／blockedとして残します。ビルド不能を「実装完了」に置き換えません。
外部依存を追加する場合は機能、サイズ、ライセンス、代替案と推移的依存を [08](08_DECISIONS_AND_REFERENCES.md) に追記します。
