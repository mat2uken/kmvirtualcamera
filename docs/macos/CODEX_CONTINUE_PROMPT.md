# Codexへの継続実装指示

以下をそのまま次の作業セッションに渡してください。

---

対象は `mat2uken/kmvirtualcamera` の `feature/macos-coremediaio-foundation` ブランチです。
Windows版のブラウザ→WebRTC→仮想カメラにmacOSを追加します。
既存コードを読み、C++20共有、最小依存、Apple標準APIを優先して、このブランチの基盤から実装を続けてください。
mainへ直接commit／mergeせず、勝手に新しい大規模frameworkや別通信protocolへ置換しないでください。

最初に `MACOS_PORT.md`、`docs/macos/07_STATUS_AND_HANDOFF.md`、`01_CODE_AUDIT.md`、`02_ARCHITECTURE.md`、
`04_MACOS_INTEGRATION.md`、`05_IMPLEMENTATION_PLAN.md` を読み、実コードと突き合わせてください。
ここにはコードがある部品、宣言だけの境界、未実装、未検証を明記しています。完成版macOS受信アプリはまだありません。

## 最初のゴール

1. ブランチ状態を確認し、共通C++テストを再実行する。
2. Mac上で `sh scripts/test_macos_foundation.sh` を実行する。実SDKのselector・型・ARCエラーを確認して修正する。
3. Xcodeの最小AppKitホストとCamera Extensionターゲットを作る。Provider/Device/sourceを実装し、
   720p30／420vの生成映像を通常のセキュリティ設定でカメラとして取得できるところまで通す。
4. アプリ側sink publisher、sink stream、producer認証、`KMFrameRelay` を接続し、ホスト生成映像をsourceへ出す。
5. ここまでの結果と未完了項目を状態文書へ記録する。環境が揃う場合は続けて共通session/RTC→VideoToolboxまで統合する。

Mac／署名環境がない場合、ネイティブbuild／installは未実施と明記する。
それを理由に作業全体を止めず、共通状態機械・パーサー・fake transport・Windows回帰など実行可能な作業を進める。
未実行のログやcamera成功結果を作らないこと。

## 守る設計

- 通信とsessionは受信アプリの共通C++へ置く。Camera ExtensionはRTC/TLS/decoderに依存させない。
- 旧DALを新規実装しない。標準CoreMediaIO sink→sourceを優先し、独自IPCを先に増やさない。
- macOSのCVPixelBuffer／IOSurfaceを共通化のためCPU配列へ強制コピーしない。
- Windowsのデコーダ／D3D／Publisher／DLLはOS層へ残す。UIのHINSTANCE/HWNDを共通ヘッダーに入れない。
- timestampをRTP90kHz、sender microseconds、local monotonic、host outputに区別する。
- compressed AUは参照依存を保ち、queue overflowを無視しない。decoded frameだけをlatest-winsにする。
- callback/reset/decode/stopの単一ownerとgenerationを実装する。raw thisの寿命を監査する。
- sink producerの認証とsource consumerの許可を混同しない。全producer許可を完成扱いにしない。
- CMSampleBuffer／CVPixelBuffer／CMSimpleQueueのretain/releaseを文書化し、満杯と切断時も試験する。
- sourceクライアントの参照数を管理し、プレビュー停止とエンジン停止を分離する。
- SIP無効化、秘密情報のcommit、無条件の受信映像dumpを導入しない。

## 先に直すべき既存問題

監査R01..R04のRTP/RTCP境界チェックと音声の誤ったPCM扱いは未修正です。回帰テストを追加して修正してください。
ブラウザの255chunk超、TURN設定の欠落、DC timer未接続、time domain混在、decoder別スレッド初期化も順に対処します。
共通へ移動したRTP/RTCPはbyte同一の既存実装で、安全性監査完了を意味しません。
Windowsの転送cppと共通ライブラリを二重リンクしないようにCMakeを整理します。

## macOS部品の既知の制限

`VideoToolboxDecoder` はAU単位waitのスモーク向けです。bounded非同期化は実測を伴って行います。
`Normalize720p` はclean aperture/SAR attachmentを一律拒否するため、恒等かどうかを判定し必要な正規化を実装します。
SPS/PPSは現在1対管理です。実fixtureを使って分割parameter setやresolution切替の必要性を確認します。
relayはflow-control／scheduled-outputを実SDKと実機で検証し、superseded sequenceやsource停止時を正しく扱う必要があります。
`KMExtensionManager` のrequest_completedをstreaming/availableと同一視せず、再列挙で状態を確定します。
entitlements.exampleのidentifierは例であり、実TeamとApp Groupへ展開して署名設定を検査します。

## 試験・成果

小さなコミットごとに目的、対象ファイル、実行コマンド、結果、未確認の環境を記録します。
共通テストを維持し、RTP/DC双方、Safari/Chrome、回転・カメラ切替・再接続・複数consumer・sleep/wakeを試験します。
メモリ安全性のsanitizer、長時間／時刻wrap、queue満杯、producer切断と拒否も対象です。
正式配布の署名・notarizationは開発ビルドと別の完了条件です。

最終報告では、実装したコード、ビルド結果、実機で確認した範囲、未実装・未検証の範囲、次の具体的作業を分けてください。
単なる設計の再説明で終わらず、実行できるところまでコードと試験を進めてください。
