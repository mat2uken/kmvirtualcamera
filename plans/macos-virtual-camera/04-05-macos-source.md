# 段階4–5: Mac部品から生成映像カメラまで

目的は、実SDKでMacの部品を確かめ、署名済みCamera Extensionのsourceから動く生成映像を取得すること。ネットワークをつなぐ前に、Extensionの導入と映像送出を独立して確認する。[macOS統合資料](../../docs/macos/04_MACOS_INTEGRATION.md)と[Apple一次資料](../../docs/macos/08_DECISIONS_AND_REFERENCES.md)を起点にする。

## 段階4: native部品とhostの土台

**開始条件**: Apple Silicon Mac、選択したXcode/macOS SDK、CMakeを用意する。開発用署名が可能か、Team ID・bundle ID・App Groupの候補を確認する。製品値は未決定のままでよい。

1. `sh scripts/test_macos_foundation.sh` を実行する。Xcode、SDK、macOS、CPU、deployment target 12.3、build/CTest結果を記録する。`km_macos_media_smoke` はカメラ登録を試験しない。
2. `macos/receiver/video_toolbox_decoder.mm` の実H.264単一AU復号を、SPS/PPSとIDRを含む正しいfixtureで確認する。`km_macos_media_smoke --decode-au <fixture>` で画像寸法と `hardwareActive()` を記録する。疑似NAL列をデコーダの正常入力にしない。
3. `macos/receiver/cv_nv12.mm` で実デコーダ出力のstride、形式、geometry、色を調べる。恒等指定と変換を要する入力を区別し、現行の一律拒否で映像が止まる場合は直す。420vへの正規化結果を画像で比較する。
4. `macos/KMVirtualCamera.xcodeproj` にAppKit hostとCamera Extensionの2 targetを作る。`macos/host/main.mm` と `app_delegate.mm` で起動、状態表示、終了を扱う。`km_macos_*` 静的部品を必要なtargetだけへリンクし、Extensionへlibdatachannel、TLS、decoderをリンクしない。
5. AppleのCamera Extensionテンプレートと実SDKを照合する。Info.plist、Extension配置、minimum OS、署名、`macos/config/*.entitlements.example` の展開結果を確認する。既存の `KMExtensionManager` を有効化に接続する。承認待ち、再起動待ち、失敗、要求完了を別々に表示し、完了後にdeviceを再列挙する。

**完了条件**: 同じSHAでMacの静的部品、CLI復号、host/ExtensionのXcode buildが成功する。hostが起動し、有効化要求の状態を表示できる。生成した署名設定とリンク依存を確認し、秘密情報をrepoへ保存しない。ここではdeviceの映像取得を完了扱いにしない。

## 段階5: Provider・Device・source

**開始条件**: 段階4のhost/Extension buildが通り、通常のセキュリティ設定でユーザー承認を行えるMacを用意する。Extension導入状態と既存の同名cameraを先に記録する。

1. `macos/camera-extension/main.mm`、`provider.mm`、`device.mm`、`source_stream.mm` を実装する。Providerから1 Deviceを登録し、stableなdevice/stream IDをrepoで保持する。表示名だけで再識別しない。sourceと後続sinkはdirectionとIDで区別する。
2. source formatを1280×720、30fps、`420v`、host clockの時刻に固定する。利用アプリがsourceを開始・停止した回数を管理する。複数consumerの一方だけが停止しても出力を止めない。この数を段階6で `KMFrameRelay.setSourceActive:` へ接続する。
3. frame counterや動く図形を描いた生成映像を送る。絶対時刻の30fps pacingでsampleを送る。`sendSampleBuffer:discontinuity:hostTimeInNanoseconds:` を使い、PTS・duration・host timeを調べる。静止した黒画像だけを成功判定にしない。
4. 生成映像の送出とsource開始・停止を1本のprovider queueへ載せる。sink接続前のこの段階では、sourceから生成映像と待機画面を送る。段階6でrelayへ移す際も同じqueueを使う。
5. hostを `/Applications` に置いてExtensionを要求し、ユーザー承認、必要な再起動、列挙、一般のカメラ利用アプリでのcaptureまで順に試す。`request_completed`だけでは使用可能と判定しない。停止・再開始と2つ以上のconsumerでも生成映像が継続することを確認する。

**完了条件**: 通常設定のMacで署名済みExtensionが列挙され、少なくとも1つの一般アプリが動く生成映像をcaptureできる。公開format、fps、時刻、複数consumer、停止後の状態を記録する。導入・列挙・captureの証拠を分け、環境、署名識別子、実行SHAを[記録様式](verification.md)に残す。SIP無効化を製品導入手順にしない。

## 実装上の注意

`frame_relay.mm` と `extension_manager.mm` は部品であり、ProviderやAppKitアプリを自動生成しない。Objective-C++のselector、Core Foundation参照、Extension delegateの型は実SDKとビルドで確かめる。初期deployment targetはCamera ExtensionのAPI下限12.3とする。製品サポートOSは段階8の試験で決める。
