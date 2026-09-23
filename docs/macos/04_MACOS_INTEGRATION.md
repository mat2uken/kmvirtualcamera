# 04 macOS統合: 次の実装を行う場所

## 現在生成されるもの

root CMakeの `KM_BUILD_MACOS=ON` は静的ライブラリとメディアCLIを生成します。
Xcodeのアプリ／Camera Extensionターゲット、Provider、Device、StreamSourceはまだありません。
ここを実装して初めてOSへカメラを登録できます。relayのコンパイル成功だけをカメラ動作確認にしないでください。

## M01: SDKコンパイルとバッファ試験

`sh scripts/test_macos_foundation.sh` をApple Silicon Macで実行し、macOS/Xcode/SDK/archを記録します。
特にObjective-Cのselector、ARCのCF参照、CMIOの列挙型、SystemExtensionsのdelegateを実SDKと照合します。
CLI黒画面試験、実際の単一AUのVTデコードを通します。clean aperture/SAR付き出力は、現状の明示的拒否を確認してから対応します。

## M02: Xcodeの最小ホスト＋Camera Extension

`macos/KMVirtualCamera.xcodeproj` を作り、ホストのAppKitターゲットとCamera Extensionターゲットを用意します。
AppleのCamera Extensionテンプレートを構造の基準にし、必要なInfo.plist、system extension配置、署名設定を実環境で確定します。
テンプレートがSwiftでも、CoreMediaIO境界はObjective-C++へ寄せ、ネットワークをExtensionへ持ち込みません。
shared静的ライブラリのbuild/linkとApple bundle/sign処理の境界を保ちます。

追加予定ファイル:

| ファイル（予定） | 責務 |
|---|---|
| `macos/host/main.mm`, `app_delegate.mm` | ウィンドウ、状態表示、終了管理 |
| `macos/camera-extension/main.mm` | providerサービスの起動 |
| `macos/camera-extension/provider.mm` | device登録、clientの接続／切断 |
| `macos/camera-extension/device.mm` | device properties、固定format |
| `macos/camera-extension/source_stream.mm` | source properties、format、開始停止とconsumer数 |
| `macos/camera-extension/sink_stream.mm` | sink properties、producer認証、供給開始停止 |
| `macos/receiver/cmio_sink_publisher.mm` | アプリ側の列挙・queue・投入・再接続 |

deviceとstreamのIDは固定・安定したものを生成してリポジトリに保持します。
表示名だけでデバイスを識別しません。source/sinkはdirectionとIDで区別します。
sourceを固定720p30／420v／host-timeとして公開し、最初はWebRTCなしで生成映像を出します。
アニメーションやフレームカウンターを付けて、黒画面の静止画だけでは確認できないpacingを検証します。

## M03: producer検証とrelayの接続

`KMFrameRelay` の `setAuthorizedProducer:` は認証を実行するメソッドではなく、**認証を終えたclientを受け取る接続点**です。
source consumerを自社アプリだけに限定してはいけません。一方sink producerは当初自社ホスト1つだけに限定します。
公開APIと実SDKで取得可能なclient識別・code signing情報を確認し、署名要件を検証する実装を入れます。
単なるPIDや表示名、未検証のsigningID文字列の一致だけを信頼根拠にしません。
認証の仕組みが確定しない間、全client許可を配布可能な暫定実装にしないでください。

認証成功・供給開始時に `setAuthorizedProducer:client`、切断時にnilを渡します。
source開始／停止はclient数を数えて `setSourceActive:` へ反映します。
全relay呼び出しを同じserial provider queueに載せ、時刻はhost clockをnsへ変換して `tickAtHostTimeNs:` に渡します。
producerが存在する間はsourceが0でもtimerと消費を止めず、古い映像が保持され続けないようにします。

## M04: アプリ側sink publisher

CoreMediaIO C APIでstable device/stream IDを列挙し、sink directionを見つけます。
`CMIOStreamCopyBufferQueue` でqueueを取得し、stream開始・停止とqueue変更通知を実装します。
正規化したCVPixelBufferからformat descriptionとCMSampleBufferを作り、有限queueへ投入します。

publisherは次の状態を明示します: unavailable / opening / ready / backpressure / disconnected / stopped。
queue満杯時にWebRTC受信を待たせず、decodedフレームだけを最新優先で置き換えます。
成功したenqueue、失敗したenqueue、queue callback、stop時の残留sampleについて所有権表を作り、参照の二重解放・リークを試験します。
単にcallbackから得たCMSampleBufferを保存するのではなく、APIの借用／所有権契約に従います。

relayにはconsume、source.send、notifyScheduledOutputChangedの接続コードがありますが、本番のflow-control契約は未検証です。
飛ばされたsequence、source非稼働時の消費、最後のsampleの重複出力、再接続前のoutstanding consumeを検証し、通知すべきタイミングをSDK／実動作で確定します。
sourceを30fpsで出すため、到着ごとに即sendして無制限にrateが上がる実装にしません。

## 有効化・署名・配布

既存 `KMExtensionManager` はactivation/deactivationの要求とdelegate結果を扱います。
request_completedは「カメラが利用可能」「映像が受信中」を意味しません。完了後に列挙し直して状態を確定します。
approval_required、reboot_required、request_failedをUIから判別可能にします。

ホストとExtensionのTeam/identifier/App GroupをXcodeで一致させます。
`config/*.entitlements.example` の変数は例です。未展開のまま署名へ使わず、生成したentitlementsを確認します。
ホストのsystem-extension.install、サンドボックス時のnetwork client/server、ExtensionのsandboxとApp Groupなどを実際の構成に合わせ最小化します。
物理カメラをMacでキャプチャしない初期ホストに、不要な画面収録・マイク権限を追加しません。
CoreMediaIO列挙や利用側アプリに必要な権限はSDKと実機で確認し、インストール権限と混同しません。

通常のセキュリティ設定で導入試験を行い、SIP無効化やシステム保護の恒久的変更を製品の手順にしません。
`/Applications`配置、ユーザー承認、バージョン更新、deactivation、削除後の状態、再起動要求を試験します。
配布用署名とnotarization、entitlements検査は、動く開発ビルドとは別の合格条件です。
署名秘密鍵、Apple ID、認証token、プロビジョニングの秘密情報をリポジトリへ保存しません。

Apple公式の参照先は [08](08_DECISIONS_AND_REFERENCES.md) にあります。
