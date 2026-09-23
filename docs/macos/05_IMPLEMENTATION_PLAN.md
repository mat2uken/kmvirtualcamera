# 05 更新版実装計画

適用基準: f7c7eb3。R01–R13には [対策コード](09_REVIEW_FIXES.md) を追加済みです。
次の作業は問題一覧を再記載するだけではなく、実依存とSDKでの確認から始めてください。

| 段階 | 実施内容 | 完了条件 |
|---|---|---|
| 0 | 修正パッケージを対象branchへ適用、diff確認、commit | 基準SHAとclean treeを確認し、mainを変更しない |
| 1 | 共通CTest、Opus有効/無効、ブラウザprotocol試験 | Debug/sanitizerとReleaseで結果を記録 |
| 2 | Windows Receiver/RTC試験targetをbuild | SDK/API差異、link不足を解消。仮想カメラ登録を伴う試験は別途明示実行 |
| 3 | Windows再接続、RTC timeout、WASAPI、UDP TURN | Close中callback、sleep/wake、48k endpoint以外を含む。frames/elementsの不一致がない |
| 4 | Mac native componentsをbuild | 実SDKの宣言に合わせ修正、CVPixelBuffer/VideoToolbox smokeを通す |
| 5 | 署名したCamera ExtensionにProvider/Device/sourceを実装 | 通常security設定のMacで生成映像をcaptureできる |
| 6 | 同じDeviceへsinkを追加、host-side publisherを実装 | host→sink→sourceで生成映像が届く。queue ownership/producer認証/複数consumerを確認 |
| 7 | SessionClient+NSURLSessionと共通RTCを接続 | ブラウザの両映像経路からMac仮想カメラまで届く |
| 8 | 非同期VideoToolbox/回転/色/負荷/長時間を最適化 | stale frame、メモリ増加、遅延蓄積を防止。コピー回数を計測 |
| 9 | Core Audio出力/仮想音声デバイス | Opus PCMをOS出力へ接続。映像Extensionと仮想マイクを混同しない |

## 具体的な変更先

段階2–3: `windows/receiver/{app,rtc,signaling,audio}`、`cmake/opus.cmake`、Windowsのターゲット設定。
段階4: `macos/receiver`、`macos/tools`。
段階5–6: 新規host/Xcode/Provider/Device/Streams/SinkPublisherと既存`KMFrameRelay`の接続。
段階7: 既存SessionClientへMac HTTP adapterを接続し、UIやnative bufferを共通公開型に流出させない。
段階8: 既存decoderの待機方式をbounded outstanding+世代で置き換え、正規化のgeometry/colorを実測。

変更後は [07](07_STATUS_AND_HANDOFF.md) の実行済み/未実行を更新し、ログを添付します。
