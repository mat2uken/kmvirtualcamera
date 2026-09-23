# Mac先行 実行計画（段階4→5→6、段階7は依存分離）

作成日: 2026-09-23。[READMEの実施順](README.md)に対し、macOS対応を最優先する範囲で実行順を具体化した計画である。対象は段階4（native部品とhostの土台）、段階5（署名済みExtensionの生成映像）、段階6（sink・producer認証・host投入）をMac完結で進め、段階7（ブラウザ映像接続）はWindows段階3への依存を分離して記載する。段階2–3のWindows作業は別枠として後続に残し、段階8以降はREADMEの各文書に従う。

## 目的と最初の到達点

目的はREADMEと同じく、通常設定のMacでブラウザ映像を仮想カメラとして表示することである。この計画の範囲内では、その手前に次の2つの到達点を置く。

- **到達点A（段階5完了）**: 署名済みCamera Extensionが列挙され、一般アプリが生成映像（720p30／420v、動く図形）をcaptureできる。
- **到達点B（段階6完了）**: hostの既知映像がsink経由でsource consumerへ届き、producer認証と待機画面の挙動が確認できる。

段階7の完了は、下記の依存分離を解消した後に行う。

## 開始時記録（実測）

| 項目 | 値 |
|---|---|
| repo / branch | `kmvirtualcamera-macos-coremediaio-foundation` / `feature/macos-coremediaio-foundation` |
| HEAD | `fdf5cab939c73f5fdaea931f6094d72c4b8a11df`（remoteより4コミット先行、未push） |
| 未コミット差分 | `plans/`（本計画を含む）が未追跡 |
| OS / CPU | macOS 26.7 (25G229) / Apple Silicon (arm64) |
| Xcode / SDK | Xcode 26.6 (17F113) / macOS SDK 26.5 |
| compiler / CMake | Apple clang 21.0.0 / CMake 4.3.4 |
| deployment target | 12.3（`scripts/test_macos_foundation.sh` の指定） |

段階を始めるたびにこの表のHEAD・差分・環境を更新し、[記録様式](verification.md)に試験結果を紐付ける。

## 実施順

| ID | 作業 | 依存 | 到達点 |
|---|---|---|---|
| M1 | 段階4: native部品の実SDK確認、Xcodeプロジェクト、host/Extension target | 環境（充足） | Xcode build成功、host起動と有効化状態表示 |
| M2 | 段階5: Provider/Device/source、生成映像capture | M1 | 到達点A |
| M3 | 段階6: sink stream、producer認証、host側publisher | M2 | 到達点B |
| M4-a | 段階7のMac側先行分: `IHttpTransport`、共通ReceiverEngine化、Mac `IVideoPipeline` | M3と並行可 | 実ブラウザ接続の土台（未接続） |
| M4-b | 段階7のWindows依存分: 段階3のRTC・TURN実動作確認 | Windows SDK端末 | 依存解消の記録 |
| M4 | 段階7: ブラウザ2経路→仮想カメラ接続 | M3・M4-a・M4-b | READMEの最初の到達点 |
| M5 | 段階8以降 | M4 | READMEの段階8–10に従う |

M4-aはM1–M3と並行して着手できる。M4-bはWindows端末がなければ「未実施（端末なし）」と記録し、M1–M3の進行を止めない。ただしM4の完了判定はM4-bの結果を含む。

## M1: 段階4 — native部品とhostの土台

詳細手順は[段階4–5の文書](04-05-macos-source.md)に従う。この計画での作業単位は次のとおり。

1. **W4-1 実SDK smoke**: `sh scripts/test_macos_foundation.sh` を実行し、Xcode/SDK/deployment target/build/CTest結果を記録する。
2. **W4-2 実AU decode**: SPS/PPSとIDRを含む正しいH.264 fixtureで `km_macos_media_smoke --decode-au <fixture>` を試す。疑似NAL列を正解入力にしない。fixtureがなければ実映像から作成し、生成元を記録する。
3. **W4-3 正規化の実出力確認**: `cv_nv12.mm` のstride・形式・geometry・色を実デコーダ出力で調べ、一律拒否で映像が止まる場合の扱いを決めて直す。
4. **W4-4 Xcodeプロジェクト新規作成**: `macos/KMVirtualCamera.xcodeproj` にAppKit host targetとCamera Extension targetを作る。`macos/host/main.mm`、`app_delegate.mm` を実装。`km_macos_*` 静的部品を必要なtargetだけへリンクし、Extensionへlibdatachannel・TLS・decoderをリンクしない。
5. **W4-5 テンプレート照合と有効化**: AppleのCamera Extensionテンプレートと実SDKを照合し、Info.plist・配置・minimum OS・署名・`macos/config/*.entitlements.example` の展開結果を確認。既存 `KMExtensionManager` を有効化要求に接続し、承認待ち・再起動待ち・失敗・要求完了を別表示、完了後にdevice再列挙。

**完了条件**: 同一SHAでMac静的部品・CLI復号・Xcode build成功、host起動と有効化状態表示。署名設定とリンク依存を確認し、秘密情報をrepoへ保存しない。deviceの映像取得はまだ完了扱いにしない。

**判断待ちの扱い**: Team ID・bundle ID・App Groupは未確定のまま開発用の識別子を使う。entitlements.example を製品値へ書き換えない。

### M1 結果記録（2026-09-23、HEAD `fdf5cab939c73f5fdaea931f6094d72c4b8a11df`＋未コミット差分）

| 作業 | 結果 | 根拠 |
|---|---|---|
| W4-1 実SDK smoke | 成功 | `sh scripts/test_macos_foundation.sh` → CTest 5/5 |
| W4-2 実AU decode | 成功 | ffmpeg 8.1.2製 `work/fixtures/au_720p30_idr.h264`（SHA256 `b7725ca8…`、SPS/PPS/SEI/IDR）。`--decode-au` → 1280x720 hardware=1、exit 0 |
| W4-3 正規化の実出力確認 | 成功 | 実デコーダ出力はidentity PAR 1:1を付与。`cv_nv12.mm` の一律拒否を「非identity clean aperture／非1:1 PARのみ拒否」へ修正。720p→identity、640x360→letterbox、PSNR約48dB。ctest 5/5継続 |
| W4-4 Xcodeプロジェクト | 成功 | xcodegen 2.46.0で2 target生成。`xcodebuild -scheme KMVirtualCamera -configuration Debug` → **BUILD SUCCEEDED**（Extension へ libdatachannel/TLS/VideoToolbox なしを `otool -L` で確認） |
| W4-5 テンプレート照合・有効化接続 | 成功 | AppleテンプレートとOBS実bundleを照合（SYSX配置・MachServiceName展開・entitlements一致を確認）。host起動→状態「待機中」、デバイス再列挙でカメラ5台を表示 |

- **実行SHA/環境**: HEAD `fdf5cab9…`（4コミット先行・未push）、macOS 26.7 / Apple Silicon、Xcode 26.6 (17F113) / SDK 26.5、Apple clang 21.0.0、deployment target 12.3。
- **署名**: DEVELOPMENT_TEAM `K7VNGA9K78`（開発用仮値、製品値ではない）、Apple Development `M4FBGCLF45`。このTeamはSystem Extension capability可。ローカルMac（destination `id=00006034-001401141462001C`、UDID `F0DABF72-C19F-5306-81EC-DE08D5505874`）をデバイス登録し Mac App Development profile を `-allowProvisioningDeviceRegistration` で自動生成。
- **artifact**: host `KMVirtualCamera` SHA256 `3f7d57fa…`、Extension `KMVirtualCameraExtension` SHA256 `88441e52…`（いずれも `macos/DerivedData/.../Debug/`、gitignore対象）。バンドルは `Contents/Library/SystemExtensions/` にSYSX配置、MachServiceName `K7VNGA9K78.com.mat2uken.kmvirtualcamera.camera-extension` を確認、`codesign --verify` 通过。
- **リンク依存**: host = AppKit/Foundation/CoreMediaIO/SystemExtensions。Extension = Foundation/CoreMediaIO/CoreMedia/CoreVideo のみ（libdatachannel・TLS・VideoToolbox なし = 共通ルール4適合）。
- **試験中に直した不具合**: (1) xcodegenのFramework指定は `frameworks:` ではなく `dependencies:` の `sdk:`（誤記は無視され未解決シンボルになった）。(2) SDKに無い `CMIOHardwareSystemGetProperty` を使用 → `kCMIOObjectSystemObject`＋`CMIOObjectGetPropertyData` へ修正。(3) hostがApp Sandbox下でカメラデバイス0台 → `com.apple.security.device.camera` entitlement と `NSCameraUsageDescription` を追加し5台列挙を確認（非sandboxのCLIでは権限なしで5台、sandboxが原因と切り分け）。
- **証跡**: `work/records/M1_host_stage4_device_list.png`（状態「待機中」・カメラデバイス5台・有効化/無効化/デバイス再取得の3ボタン）。
- **未実施**: 有効化要求の実機実行（承認フロー・再起動・列挙）は段階5のW5-4へ回す。M1完了条件は「host起動と有効化状態表示」までで、deviceの映像取得は完了扱いにしない。

## M2: 段階5 — Provider・Device・source

1. **W5-1 部品実装**: `macos/camera-extension/main.mm`、`provider.mm`、`device.mm`、`source_stream.mm` を実装。Providerから1 Deviceを登録し、stableなdevice/stream IDをrepoで保持する。表示名だけで再識別しない。
2. **W5-2 format固定**: source formatを1280×720・30fps・`420v`・host clockに固定。consumer利用数を管理し、一方の停止で出力を止めない（この数をM3で `setSourceActive:` へ接続）。
3. **W5-3 生成映像**: frame counterと動く図形を絶対時刻30fpsで `sendSampleBuffer:discontinuity:hostTimeInNanoseconds:` により送出。静止黒画像を成功判定にしない。source開始/停止を1本のprovider queueへ載せる。
4. **W5-4 実機試験**: hostを `/Applications` に置き、Extension要求→ユーザー承認→再起動→列挙→一般アプリcaptureまで順に試す。`request_completed`だけを使用可と判定しない。停止・再開始と2 consumer以上で継続を確認。

**完了条件**: 通常設定のMacで署名済みExtensionが列挙され、一般アプリが動く生成映像をcaptureできる。導入・列挙・captureの証拠を分け、環境・署名識別子・実行SHAを[記録様式](verification.md)に残す。SIP無効化を導入手順にしない。

### M2 結果記録（2026-09-24、HEAD `cddf05b`＋M2未コミット差分）

| 作業 | 結果 | 根拠 |
|---|---|---|
| W5-1 部品実装 | 成功 | `macos/camera-extension/` に provider/device/source_stream/ids を実装。`xcodebuild -scheme KMVirtualCamera -configuration Debug` → **BUILD SUCCEEDED**（警告0）。`sh scripts/test_macos_foundation.sh` → CTest 5/5 |
| W5-2 format固定・consumer計数 | 成功 | 1280×720 / 30fps / `420v` / host clockに固定。Photo Boothのセッション再起動で `stopStream (consumers=0)` → `startStream (consumers=1)` を観測。2 consumer時はframeworkが `add streaming client` で吸収しsource再startなし＝送出無停止 |
| W5-3 生成映像30fps送出 | 成功 | 7セグメントカウンタ＋三角波形状を絶対host時刻で `sendSampleBuffer:discontinuity:hostTimeInNanoseconds:` により送出。`w5-4-frame-timing.txt`: 150フレーム=5.000秒ちょうど（30.000fps）、PTS単調、初回Unknown後 flags=0（drop 0） |
| W5-4 実機試験 | 成功 | `/Applications` 配置→有効化要求→`approval_required`→ユーザー承認→`systemextensionsctl` **[activated enabled]**（0.1.0/4）。列挙: host 6台 / system_profiler / AVFoundation / Photo Booth・QuickTimeの一覧に表示。capture: Photo BoothとQuickTimeが生成映像を表示しカウンタ・矩形が実時間で変化。2 app並行でも継続 |

- **実行SHA/環境**: HEAD `cddf05b`（7コミット先行・未push）＋M2未コミット差分。macOS 26.7 (25G229) / Apple Silicon、Xcode 26.6 (17F113) / SDK 26.5、deployment target 12.3。デバイス `id=00006034-001401141462001C`、`-allowProvisioningUpdates -allowProvisioningDeviceRegistration`。
- **署名・識別子**: DEVELOPMENT_TEAM `K7VNGA9K78`（開発用仮値、製品値ではない）。device `c1b67446-47cf-4d2e-9c5d-76a56127f3de` / stream `ab8614be-9eff-45bb-9c47-4ee598fe128f`（`macos/camera-extension/ids.h` で固定）。`codesign --verify --deep --strict /Applications/KMVirtualCamera.app` 通过。承認は「システム設定 > 一般 > ログイン項目と拡張機能」で実施、SIP変更なし。
- **試験中に直した不具合**: (1) sysextdは埋め込み `.systemextension` ディレクトリ名＝CFBundleIdentifierを要求し、不一致では `bundle identifier and service path did not match` で「Extensionが見つからない」になる → `macos/project.yml` のExtension `PRODUCT_NAME` をバンドル識別子に修正（実行ファイル名は `EXECUTABLE_NAME` で維持）。(2) `CMSampleBufferCreateForImageBuffer` が `-12743`（`kCMSampleBufferError_InvalidMediaFormat`）で全フレーム失敗しプレビュー黒画面 → ピクセルバッファのBT.709 attachmentとFormatDescription拡張の不一致が原因。FormatDescriptionにもBT.709拡張を付与して送出回復（Swift再現実験で確認: attachment有り＋拡張無しなら同エラー）。(3) Extension版数更新跨ぎで長時間起動したhostのCMIO列挙が5台のまま → host再起動で6台（試験運用上の注意、製品不具合ではない）。旧版1–3は `terminated waiting to uninstall on reboot`（再起動で削除される）。
- **証跡**（`work/records/`、gitignore対象）: `w5-4-activation-evidence.txt`（有効化状態・6台列挙・署名確認）、`w5-4-activated-device-list.png`（host 6台表示）、`w5-4-photobooth-generated-frame1/2.png`（Photo Booth表示・カウンタ変化）、`w5-4-quicktime-live-frame1/2.png`（QuickTime 316468→316534 ≒ +66フレーム/2秒）、`w5-4-two-consumers-quicktime-photobooth.png`（2 app並行）、`w5-4-frame-timing.txt`（30fps送出ログ）。
- **未実施・判断待ち**: 長時間（30分）、遅延・queue深さ・CPUの許容値測定は段階8へ。送出時ログ（150フレーム毎のNSLog）は試験用のまま。到達点A（段階5完了）達成。

## M3: 段階6 — sink、producer認証、host投入

詳細は[段階6の文書](06-sink-publisher.md)。作業単位:

1. **W6-1 sink stream追加**: sourceと別のstable ID・direction・固定formatで公開。source利用数を `setSourceActive:` へ接続。
2. **W6-2 producer認証**: `CMIOExtensionClient` の実取得可能識別情報とOS検証署名でhostを確認。PID・表示名・未検証signing IDのみで許可しない。許可後 `setAuthorizedProducer:`、切断/世代変更でnil。
3. **W6-3 host側publisher**: CoreMediaIOでstable device ID・sink directionを列挙、`CMIOStreamCopyBufferQueue` と開始/停止/queue通知/再列挙を実装。状態を unavailable / opening / ready / backpressure / disconnected / stopped に分ける（新規 `macos/receiver/cmio_sink_publisher.mm`）。
4. **W6-4 frame投入**: hostの生成映像を720p/420vへ正規化し、format descriptionとCMSampleBufferを有限queueへ。最新優先の置換、リーク・二重releaseを試す。
5. **W6-5 consumeとrelay**: `consumeSampleBufferFromClient:completionHandler:` の結果を `KMFrameRelay` へ。1世代1件outstanding、sequence・discontinuity・`hasMore`・error処理、旧世代と切断後画像の破棄。
6. **W6-6 切り替え**: 段階5の生成映像送出をrelayへ移し、同じ30fps timerから `tickAtHostTimeNs:`。表示時刻で `notifyScheduledOutputChanged:`。source停止中もsink消費と旧画像失効を継続。

**完了条件**: 署名確認済みhostのみ投入可、720p30・単調時刻・sequence通知・backpressure・切断後待機画面が確認でき、複数consumerがproducer接続を壊さない。

## M4-a: 段階7のMac側先行分（並行着手可）

[段階7の文書](07-live-receiver.md)の手順のうち、Windows実動作を待たずに着手できるもの。

- Mac向け `IHttpTransport`（`shared/km/receiver_contracts.h`）実装: HTTPS検証、redirect禁止、期限・cancel、1MiB応答上限、signaling worker分離。
- `windows/receiver/rtc/peer_connection_manager.*` と `shared/receiver/rtc` の整理を、Windowsビルドを伴わない共通C++範囲に限定して進める。Win32/D3D/CVPixelBuffer型を共通公開型へ入れない。
- `km::EncodedVideoFrame` のowning Annex-B AUを渡すMac video workerと、Mac `IVideoPipeline`（decode・向き・geometry・色・420v正規化→publisher）。
- AppKit hostの接続UI（join URL/QR、状態、統計表示）と、TURN設定の引き継ぎ。

この単体では「接続成功」を主張しない。ビルドと単体試験のみを証拠とする。

## M4-b: 段階7のWindows依存分（段階3）

[段階2–3の文書](02-03-windows-native.md)に従う。Mac計画からは切り出すが、M4完了の必須依存である。

- Windows SDK端末で `cmake -S windows -B build/windows -DKM_ENABLE_OPUS=ON` と製品ターゲット・限定CTestを実施し、CI 3 jobをremote公開SHAで通す（段階2の一部も兼ねる）。
- MediaTrack H.264／WebCodecs DCの2経路、Opus/WASAPI、Close競合、WinHTTP失敗経路、UDP TURN relay-onlyを記録する（段階3）。
- 端末が用意できない場合は「未実施（端末なし）」と理由を記録し、M4の完了を保留にする。M1–M3は継続する。

## M4: 段階7 — ブラウザ映像接続

M3・M4-a・M4-bが揃ったら、[段階7の文書](07-live-receiver.md)の手順でローカルsignaling→実ブラウザのMediaTrack、続いてWebCodecs/DCの順に確認する。この到達点がREADMEの最初の到達点（通常設定Macでブラウザ映像を仮想カメラ表示）である。検証は[記録様式](verification.md)に従い、実ブラウザ2経路・断線復旧・Mac captureの証拠を残す。

## 共通ルール

1. 各作業単位の開始時にrepo・branch・HEAD・既存変更を記録し、対象外の変更を保持する。
2. 「ビルド」「インストール」「列挙」「映像」「長時間」「配布」を別の確認欄とし、前段の成功を後段の証拠にしない。
3. API名・可用性は実SDKヘッダーと実行結果を根拠にする。文書とコードが違えばコードを調べ、差を記録する。
4. Extensionにはdevice/stream・producer確認・最新映像・30fps送出・待機画面だけを置く。RTC・TLS・VideoToolboxは受信アプリ側に置く。OS固有の画素バッファを共通C++公開型へコピーしない。
5. 秘密鍵・トークン・証明書の中身をログやrepoへ保存しない。Team ID・bundle ID・App Group・配布方式・対象macOS版・Intel要否は製品判断待ちとし、開発用仮値を製品設定として固定しない。
6. 新protocol・大型依存・音声・Intel・OS範囲拡大が必要になれば、[設計判断](../../docs/macos/08_DECISIONS_AND_REFERENCES.md)と実測を照合して判断を記録する。

## リスクと対処

| リスク | 対処 |
|---|---|
| Camera Extension APIの実挙動が計画の想定と異なる（client識別情報、queue通知、承認フロー） | 実SDKヘッダーと署名済み実機で確かめ、差を[設計判断](../../docs/macos/08_DECISIONS_AND_REFERENCES.md)へ記録。想定変更はW6-2/W6-3の開始前に検討 |
| Windows端末が使えないためM4-bが滞在 | 「未実施（端末なし）」を継続記録し、M1–M3とM4-aを前進。段階2のCI確認にはremote公開が必要 |
| remoteより4コミット先行・`plans/`未コミットの状態が長引く | 各マイルストーンの完了時にコミット方針を決め、pushはユーザー確認後に行う |
| development署名での承認フローが通常設定Macで成立しない | M1のW4-5で早めに実機確認し、失敗時は理由とSDK差分を記録してからM2へ進む |
| fixture不足でdecode確認が曖昧になる | 疑似NALを使わず実映像からfixtureを作り、生成元・SHA256を記録 |

## 参照

- [README（残作業計画と実施順）](README.md)
- [段階4–5: Mac部品から生成映像カメラまで](04-05-macos-source.md)
- [段階6: sinkとhost投入](06-sink-publisher.md)
- [段階7: ブラウザ映像接続](07-live-receiver.md)
- [段階2–3: Windows実依存確認（M4-bの対象）](02-03-windows-native.md)
- [記録様式と受け入れ判定](verification.md)
- [状態と引き継ぎ](../../docs/macos/07_STATUS_AND_HANDOFF.md)
