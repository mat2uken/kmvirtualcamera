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

### M3 結果記録（2026-09-24、HEAD `df34a43`＋M3未コミット差分）

| 作業 | 結果 | 根拠 |
|---|---|---|
| W6-1 sink stream追加 | 成功 | `macos/camera-extension/sink_stream.*` をsourceと別のstable ID・direction=0で公開。`setSourceActive:` にconsumer計数を接続。`xcodebuild -scheme KMVirtualCamera -configuration Debug` → **BUILD SUCCEEDED**（警告0）。`sh scripts/test_macos_foundation.sh` → CTest 5/5 |
| W6-2 producer認証 | 成功（残課題記録） | 実SDKで `CMIOExtensionClient` から取得できるのは `clientID` / `signingID`（未検証文字列、cross-processでは "unknown"）/ `pid` のみ。ここにOS検証署名のcross-checkを組み合わせ、mode K（host keychainのEC P-256鍵＋`producer_auth_pubkey.h`、stages 50/60–64、ladder fallback）を実装。**初回・2回目以降とも `mode=key-attestation` を通過**。不許可sink投入は `sink start rejected (pid=...): host executable evidence missing` で拒否、同時2 producerは `CMIODeviceStartStream = -4` / `sink start rejected: another producer connected` で拒否（hostは無傷 feeds=389）。`work/sink_probe` で検証 |
| W6-3 host側publisher | 成功 | 新規 `macos/receiver/cmio_sink_publisher.mm`。状態を unavailable / opening / ready / backpressure / disconnected / stopped に分離しUI通知。device UID `c1b67446-…-76a56127f3de` を列挙しsink streamへ接続、`CMIOStreamCopyBufferQueue` とqueue変更通知・2秒retryを実装 |
| W6-4 frame投入と所有権 | 成功（試験中に1件修正） | enqueue成功でframeworkへ+1移転、hostはenqueue後のCFReleaseを禁止（enqueue失敗/満杯でのみrelease）に修正し、`created = enqueued + dropped = released`・`inFlight=0` の会計恒等式を全試験で一致確認。背圧試験: 1ms burstで容量10を満杯→ `queue full, dropped new frame`・ready↔backpressure遷移・dropped=640・created=27017=released=27017 |
| W6-5 consumeとrelay | 成功 | `consumeSampleBufferFromClient:` の結果を `KMFrameRelay` へ。陳旧challenge対策として `issueAuthChallenge` が `kKMAuthChallengeProperty` をnotify（Extension CFBundleVersion 17）。host kill -9時は `consume error code=-7` → `clearing producer (sink stopped)` → `producer cleared (gen=11)`、再起動後 gen=12 で再認証 |
| W6-6 切り替えと切断試験 | 成功 | producer停止→待機、強制終了→回復、source停止・再開（consumer退出で `stopStream (consumers=0)`、再openで直前と異なる新フレーム＝旧世代復帰なし）、2 consumer（Photo Booth 22.4% / QuickTime 11.3%のフレーム差分）、別アプリsink投入拒否を個別に実施。Photo Boothカウンタ試験・停止→黒待機も確認 |
| Extension再起動 | 成功（host再列挙の残課題を含む） | 無効化→ `[terminated waiting to uninstall on reboot]`・Extensionプロセス停止→hostは `opening → unavailable (device UID not found)` を2秒retry、会計維持（created=released=27017, inFlight=0）。有効化→ `[activated waiting for user]`→`[activated enabled]`・新pid 66275・provider初期化と同UIDでdevice再追加。**稼働中hostプロセスはdevice再検出に失敗し続ける**が独立ツール（`work/cmio_list`）は id=48 で可視。host再起動で即時に `ready`＋`attestation written status=0` に回復、Photo Booth差分2.2%で新規フレーム表示 |

- **実行SHA/環境**: HEAD `df34a43`（8コミット先行・未push）＋M3未コミット差分。macOS 26.7 (25G229) / Apple Silicon (Mac15,10)、Xcode 26.6 (17F113) / SDK 26.5、deployment target 12.3。デバイス `id=00006034-001401141462001C`、`-allowProvisioningUpdates -allowProvisioningDeviceRegistration`。Extension 0.1.0/17。
- **署名・識別子**: DEVELOPMENT_TEAM `K7VNGA9K78`、App Group・producer要件の識別子はすべて開発用仮値（製品値ではない）。ASPはsandbox強制（`macos/config/CameraExtension.entitlements`）。`codesign --verify --deep --strict /Applications/KMVirtualCamera.app` 通過。SIP変更なし。
- **試験中に直した不具合**: (1) **queue所有権の超過release**（over-releaseで3件の `.ips`）→ enqueue成功でframeworkへ所有権移転する規約に修正。(2) **陳旧認証challenge** → `issueAuthChallenge` でのプロパティnotify（v17）。(3) **背圧到達不能**（feed 1ms < drain ≒250fpsでqueueが満杯にならず）→ `kBurstPeriodNs` を4000000→1000000に短縮しbackpressure遷移とdropを観測。(4) **generator race崩壊**（`generateTick` と `stopGenerator` の競合で `dispatch_source_set_timer` にSIGSEGV、`w6-4-stoptime-crash.ips`）→ create/cancel/tickを `_genQueue` へ直列化し5回のstart/stopで崩壊なし。
- **設計・運用の記録**: (a) mode K（key-attestation）を採用、ladderはfallbackに。(b) TOCTOU残課題: executable証跡はディスク照合であり置換レースは理論上残る（mode Kのchallengeで実行時検証）。(c) container分割: hostのevidence書き込みが `/private/tmp`・`/Users/Shared` とも `Operation not permitted`（App Sandboxの帰結、製品不具合ではない）。(d) **Extension無効化は管理者権限 `com.apple.system-extensions.admin` の認証が必須**で、`OSSystemExtensionDeactivationRequest` がmain threadの `AuthorizationCreate` 同期待ちを伴う（macOS 26では `systemextensionsctl disable/enable` は無く、非包含アプリからの要求は `OSSystemExtensionErrorDomain Code=2` で拒否）。(e) **Extension再起動後に稼働中hostがdeviceを再検出できない**（段階5で観測したversion跨ぎ時と同じ現象）→ host再起動で回復。試験運用上の注意として記録し、リトライ強化は製品改善課題。(f) System Events (AX) は本アプリで不安定（window数0・要素値陳旧、認証待ちでmain thread停止すると全滅）→ 座標クリックとスクリーンショット検証で代替。
- **証跡**（`work/records/`、gitignore対象）: auth ladder一式（`auth-ladder-*.log`）、ASPログ、mode Kログ（`mode=key-attestation` 初回・2回目）、`w6-4-ownership-fix-host.log`、`w6-4-notify-props-v17-host.log`、`w6-4-burst1ms-host*.log`（背圧・drop=640）、`w6-4-stoptime-crash.ips` と over-release `.ips` 3件、`w6-4-genrace-fix-host.log`（修正後5サイクル）、`w6-photobooth-scr.png`、`w6-4-genrace-2consumers-part.log`、`sink_probe` 拒否ログ、`w6-4-extrestart-host.log`（再起動・再認証 status=0）、`w6-4-extrestart-dead.log`（切断時 retry・会計）、`w6-4-extrestart-photobooth-after.png`（再起動後の新規フレーム）、`w6-4-extrestart-axwedge-auth-sample.txt`（認証待ちスタック）、`w6-4-extrestart-authdialog-screen.png`、`w6-4-extrestart-cmio_list.c`（独立列挙ツール）。
- **未実施・判断待ち**: 長時間（30分）、遅延・queue深さ・CPUの許容値測定は段階8へ。TOCTOU残課題とExtension再起動後のhost再列挙強化は製品改善課題として記録。App Group・bundle IDの製品値化、配布方式、対象macOSバージョン、Intel対応は未決定。送出時ログの抑制は試験用のまま。

## M4-a: 段階7のMac側先行分（並行着手可）

[段階7の文書](07-live-receiver.md)の手順のうち、Windows実動作を待たずに着手できるもの。

- Mac向け `IHttpTransport`（`shared/km/receiver_contracts.h`）実装: HTTPS検証、redirect禁止、期限・cancel、1MiB応答上限、signaling worker分離。
- `windows/receiver/rtc/peer_connection_manager.*` と `shared/receiver/rtc` の整理を、Windowsビルドを伴わない共通C++範囲に限定して進める。Win32/D3D/CVPixelBuffer型を共通公開型へ入れない。
- `km::EncodedVideoFrame` のowning Annex-B AUを渡すMac video workerと、Mac `IVideoPipeline`（decode・向き・geometry・色・420v正規化→publisher）。
- AppKit hostの接続UI（join URL/QR、状態、統計表示）と、TURN設定の引き継ぎ。

この単体では「接続成功」を主張しない。ビルドと単体試験のみを証拠とする。

### M4-a 結果記録（2026-09-24–25、HEAD `d09a207`・`4347313`・`fc70312`・`b889984`・`6c146e6`・`7e1a563`・`5838dae`・`19bc4ee`・`f2ef5dd`・`caefcc2`、26コミット先行・未push）

| 作業 | 結果 | 根拠 |
|---|---|---|
| Mac `IHttpTransport`（単位1） | 成功 | `macos/receiver/url_session_transport.{h,mm}`（NSURLSession、single in-flight、redirect拒否、loopback以外の `http://` 拒否、1MiB上限、400ms期限、cancelのsticky）。試験 `macos_http_transport` はループバックのみ（127.0.0.1 bind）でSessionClient全体・redirect未追従・上限・期限・cancelを確認。コミット `d09a207` |
| Mac `IVideoPipeline`（単位2） | 成功 | `macos/receiver/video_pipeline.{h,mm}`（owning Annex-B AUを保有workerで直列デコード、bounded queue容量8、epoch管理）。試験 `macos_video_pipeline` は実H.264をVideoToolboxで試験内生成（fixture非依存）し、start検証・start前submit・世代不整合・Malformed Annex-B・SPS欠落デコード失敗からのキーフレーム回復・背圧時の依存列破棄・90°変換下の720p/420v配信・stop後のキュー無効化とhandler停止を確認。コミット `4347313` |
| ReceiverEngine共通C++化（単位3） | 成功 | `shared/receiver/engine/receiver_engine.{h,cpp}`（PCMの受信方針を集約: ペイロード型のフェイルクローズ絞り込み、SSRC変更時のunwrapリセット、DC経路優先の仲裁、50msスロットル付きキーフレーム要求とタイマ再試行、コントロール伝送、世代・時刻スタンプ、統計）。試験 `receiver_engine` は合成パケットのみで8シナリオと2万件の破損コーパスを確認。Windowsビルド不要範囲に限定し `windows/receiver/rtc/*` 本体は未変更。コミット `fc70312` |
| シグナリング専用ワーカー（単位4） | 成功 | `shared/receiver/signaling/signaling_worker.{h,cpp}`（SessionClientの作成・Offer poll・Answer送信を1本の専用スレッドへ集約。Windows `SignalingWorkerProc` と同じサーバ指定間隔・+500msバックオフ・タイムアウト巡回、cancelは `IHttpTransport::cancel` で中断してjoin、フェーズ通知とjoinUrl通知、時刻シームで仮想時刻を注入可能）。試験 `signaling_worker` は6シナリオを合成transportのみで確認: 正常系（4回poll・3秒でanswer送信・PUT JSON検証）、バックオフ/タイムアウトの決定的検証（8ポール・1120ms）、作成失敗、Answer生成不能、Answer拒否、ブロック中のcancel（2秒以内、コールバック・HTTPがすべて同一ワーカースレッドでmainではないこと）。実通信なし・ブラウザ接続なし。コミット `b889984` |
| AppKit join UI・QR・worker配線（単位5） | 成功 | `macos/host/app_delegate.mm`（受信セッション節: Signaling URL入力・開始/停止・フェーズ日本語表示・join URL表示・QR画像。`windows/third_party/qr/qrcodegen` をWindows UI同設定（Ecc::MEDIUM・quiet zone 4）で描画し、Mac transport + `SignalingWorker` を専用スレッドで配線、generationガード付きでmain queueへ集約、終了時にcancel・join）。試験 `macos_qr_roundtrip` はqrcodegen生成→グレースケールbitmap→Vision QR復号の一致3件（独立デコーダ・fixtureなし）。ビルドはxcodebuild警告0。実行証跡: ローカルsignaling（`wrangler dev` 127.0.0.1:8787、実装コード）でセッション作成→join URL/QR→API経由のOffer受信→RTC未実装による明示失敗（phase 0→1→2→3→7）を起動ログで確認し、画面QRをjsqrで復号してjoin URLと一致を確認（`work/records/m4a-unit5-*`、未追跡）。コミット `6c146e6`・`7e1a563` |
| libdatachannel導入（単位6） | 成功 | ルートCMakeの `KM_FETCH_DATACHANNEL`（Windows `legacy_targets.cmake` と同一pin: mbedTLS v3.6.2＋libdatachannel v0.22.4・mbedTLS・static、CMake 4互換の `CMAKE_POLICY_VERSION_MINIMUM`、libsrtp `TEST_APPS` 抑制）で `km_rtc_shared`（`peer_connection_manager.cpp`・`shared/receiver/rtc` 含む）をmacOSでビルド・リンク（`-Wall -Wextra -Wpedantic` で警告0、pcmのWin32シンボル0を実測）。試験 `rtc_load` はオフライン（Offer・gathering・接続なし）でPC生成／破棄と設定ゲート6件（policy all成功、relayで無TURN拒否、UDP TURN受容、`turns:`（TLS）不採用時のrelay拒否、資格情報なし拒否、不正スキーム拒否）を確認。`sh scripts/build_macos_rtc.sh` → **11/11 pass**・自社コード警告0（`work/records/m4a-unit6-rtc-final.txt`）。判断はD11。新規フルビルドで判明した `CVBufferGetAttachment` のdeprecation（`cv_nv12.mm`・`media_smoke.mm`）を `CVBufferCopyAttachment` へ修正して警告0を維持。pinned mbedTLSは新規フルビルド時のみApple Clang 21の資材由来警告（自社コード外）。コミット `5838dae`・`19bc4ee` |
| CTestゲート | 成功 | `sh scripts/test_macos_foundation.sh` → 単位1–2で **7/7 pass**、単位3で **8/8 pass**、単位4で **9/9 pass**、単位5で **10/10 pass**、単位6–8でも **10/10 pass**（非推奨修正後の再ビルドで警告0、ビルド・試験のみ・インストールなし）。RTC変種（`scripts/build_macos_rtc.sh`）は 単位6で **11/11 pass**（`rtc_load` 追加）、単位7で **12/12 pass**（`rtc_answer` 追加） |
| makeAnswer RTC配線・TURN引き継ぎ（単位7） | 成功 | `macos/host/app_delegate.mm` の makeAnswer が `PeerConnectionManager::Initialize(session.rtcConfiguration, …)` → `ProcessOfferAndGenerateAnswer` をworkerスレッドで実行し、サーバがCreateSessionに付帯するICEポリシー・TURN設定をそのまま引き継ぐ（relay+UDP TURN受容・`turns:` relay拒否のゲートは単位6 `rtc_load` と同一経路をセッション設定経由でも確認）。stopはWindows同順で `CancelPending` → cancel/join → `Close`。試験 `rtc_answer`（合成transport＋実pcmの3シナリオ: 合成Offer→実Answer生成でphase 0→1→2→3→4→5・Answer SDP検証・Connected不発・フレーム0、relay+UDP TURNの初期化受容、`turns:` 拒否でphase 7明示失敗）→RTC変種 **12/12 pass**（`work/records/m4a-unit7-rtc-test.txt`、未追跡）。依存ビルド `build_macos_xcode_deps.sh` はfoundation（拡張・ゲート、RTC無し）とrtc（アプリ依存、pinned RTC stack）の2ツリーに分離し、アプリのLDFLAGS/検索パスを `build/macos-rtc` 配下へ（libdatachannelヘッダは `-isystem` で資材の `-Wdocumentation` を抑制）。xcodebuild警告0、拡張バイナリのRTC/VideoToolboxシンボル0を実測。実行証跡: ローカルwranglerで claim→合成Offer投入→phase 0→1→2→3→rtc state=1→answer 845 bytes→4→5（`work/records/m4a-unit7-*`）。コミット `f2ef5dd` |
| 映像パイプライン供給・統計UI（単位8） | 成功 | `macos/host/app_delegate.mm`（pcm videoCbのAnnex-B AUを `EncodedVideoFrame`＝mediaTicks µs＋`SenderMicroseconds`＋`BoundedVideoQueue::configuredIdr`＋run世代で `VideoPipeline::submit` へ、`NeedKeyframe`/`Backpressure` はWindows push失敗経路と同じく `RequestKeyframe`。パイプラインは開始時にmainで生成し世代はセッション世代と一致。受信初回フレームで既知映像ジェネレータを停止してsink publisherへ切替、`stopSignalingWorker` は `Close`→pipeline `stop` の順でハンドラ停止を保証。統計行は1秒毎にRTC推定ビットレート・loss・受信/配信fps・decode失敗を測定値のみ表示、`—` は非稼働）。アプリに `-lkm_macos_pipeline -lkm_macos_decoder -framework VideoToolbox` を追加（拡張ターゲット非変更、拡張バイナリのRTC/VideoToolboxシンボル0を実測）。実行証跡: ローカルsignalingでphase 0→5到達後に統計行が測定値表示、UI写真（`work/records/m4a-unit8-*`）。xcodebuild警告0。コミット `caefcc2` |

- **主張しない範囲**: 実Cloudflare API・実ブラウザとの接続、RTC接続（ICE candidate pairの確立）と実映像受信は未実施。Answer生成はオフライン試験（`rtc_answer`）とローカルsignaling実行ログ（phase 5＝Answer送信完了）で確認済み。実signalingはローカル `wrangler dev` のみで接続成立は主張しない。QRは独立デコーダ（Vision/jsqr）との照合のみで端末カメラ走査は未実施。統計・接続状態の表示は未接続時の測定経路のみ確認（Connected時の値は接続後に確認）。本記録はビルド・単体試験とローカル実行ログを根拠とする。
- **記録で延期**: `windows/receiver/rtc/peer_connection_manager.*` のReceiverEngine切替はWindowsビルドでの回帰確認が必要なため後続（M4-b端末依存）へ延期。
- **残り（M4-a）**: 07-live-receiverの手順1・3・4・5・6の接続前部分（transport、owning AU→Mac worker、映像パイプライン、join UI/状態/統計、TURN引き継ぎ）は実装・試験済み。残りは実ブラウザ接続の確立と実映像の測定（段階7の順序: MediaTrack→WebCodecs/DC）、WebCodecs/DC経路のMac側確認、音声・仮想マイク（別段階、D08）、TURN TCP/TLSはlibjuice非対応（D11）でlibnice導入は製品要件判断待ち。

## M4-b: 段階7のWindows依存分（段階3）

[段階2–3の文書](02-03-windows-native.md)に従う。Mac計画からは切り出すが、M4完了の必須依存である。

- Windows SDK端末で `cmake -S windows -B build/windows -DKM_ENABLE_OPUS=ON` と製品ターゲット・限定CTestを実施し、CI 3 jobをremote公開SHAで通す（段階2の一部も兼ねる）。
- MediaTrack H.264／WebCodecs DCの2経路、Opus/WASAPI、Close競合、WinHTTP失敗経路、UDP TURN relay-onlyを記録する（段階3）。
- 端末が用意できない場合は「未実施（端末なし）」と理由を記録し、M4の完了を保留にする。M1–M3は継続する。

**結果（2026-09-26更新）**: Windows物理端末（Windows 11 Pro、Tailscale接続）を用意し、段階2の製品ビルド・限定CTestを同一SHA `d1b9f5f` で完了した。段階3は映像2経路・音声・再接続・Close競合・WinHTTP失敗経路・UDP TURN relay-only・TCP/TLS-only設定の理由付き失敗の7項目を同一SHA `ebd51bd` で記録し完了（詳細は[段階3結果記録](02-03-windows-native.md)）。CI 3 jobは公開SHA `ebd51bd` で success（run 36150269346）。M4-bは完了。

## M4: 段階7 — ブラウザ映像接続

M3・M4-a・M4-bが揃ったら、[段階7の文書](07-live-receiver.md)の手順でローカルsignaling→実ブラウザのMediaTrack、続いてWebCodecs/DCの順に確認する。この到達点がREADMEの最初の到達点（通常設定Macでブラウザ映像を仮想カメラ表示）である。検証は[記録様式](verification.md)に従い、実ブラウザ2経路・断線復旧・Mac captureの証拠を残す。

### M4 結果記録（2026-09-25、HEAD `38bb918`（u1試験中のAnswer修正は `690e7c0`）、31コミット先行・未push）

| 試験 | 結果 | 根拠 |
|---|---|---|
| M4-u1 実ブラウザ MediaTrack経路 | 成功 | Chrome（`--use-fake-device-for-media-stream` 等、CDP 9222）→ ローカルsignaling（実装コードの `wrangler dev`、127.0.0.1:8787）→ host RTC → VideoToolbox → sink publisher → Extension → AVFoundation consumer。phase 0→5、answer 4978 bytes、rtc state 1→2、受信frames 1→8400（約20fps）、統計「送信2.80Mbps｜loss0.0%｜受信20.0fps｜配信20.0fps｜decode失敗0」、「KM Virtual Camera」1280x720へのcaptureが実ブラウザ映像（`work/records/m4-u1-*`、未追跡）。試行でChromeのanswer拒否（rejected m-lineのPT保持不足）を発見し `690e7c0` で修正、`test_rtc_answer` シナリオ4で回帰固定 |
| M4-u2 実ブラウザ WebCodecs/DC経路 | 成功 | 同一構成のDC経路。answer 1681 bytes → state 2 → 受信frames 2100超、統計「2.50Mbps｜20.0fps｜decode失敗0」、consumer captureが実ブラウザ映像（`work/records/m4-u2-*`、未追跡） |
| M4-u3 断線復旧（停止→黒→再接続） | 成功 | 同一セッションで2回反復。送信停止 → rtc state 3/5 → 最終フレームから1.018秒・1.008秒で `no new frame … -> feeding black` → consumerが完全な黒（生フレームとの差19.12、黒同士の差0.00）。再接続 → phase 3 → 新規answer（1681・1682 bytes）→ phase 5 → state 2 → `new frame arrived -> resuming live feed` → 受信frames継続 → consumerが実映像（差1.76）、`answer send failed` 0件（`work/records/m4-u3-*`、未追跡）。試行で見つけたhost側2ギャップ（停止後の最終フレーム再投入で黒timeout不発、Succeeded後の監視終了で再Offer未回答）を `ffd4de2` で修正し、sink publisherの1秒新着なしで黒送出、signaling workerの継続監視と回答済み同一Offerスキップを `signaling_worker` シナリオ7で固定 |
| M4-u4 音声無効設定（受信がaudio拒否） | 成功 | 受信host（`KM_ENABLE_OPUS=OFF`）でanswerがaudio m-lineを拒否。送信側Statsに `audioBytesSent` が現れず `videoBytesSent` のみ増加 ＝ 音声RTP未送信（offer metadata `m-lines=2 codecs=opus,…` は試験中追加の `f43b058` によるNSLog。`work/records/m4-u4-run*.txt`・`m4-u45-cdp2.txt`・`m4-u4-shot*.png`、未追跡） |
| M4-u5 カメラ切替での継続 | 成功 | 接続中に「📷 切替」操作後も `videoBytesSent` が 61609→752018→798736 と途切れず増加、videoFps 20を維持、consumerが切替後の映像を表示（`work/records/m4-u45-cdp2.txt`・`m4-u5-shot*.png`、未追跡） |
| M4-u6 縦持ち画素（portrait） | 成功 | attempt1/2はChromeの `--use-file-for-fake-video-capture` がPC connectedで供給を止め全黒（RTCなしの隔離試験では20秒300フレーム正常でChrome環境側の問題と切り分け）。attempt3は送信元を `canvas.captureStream(360x640,30fps)` のテストダブルへ切替（encode以降は実機と同一経路）: preview 360x640、`videoBytesSent` 1932→417337単調増加、rtc frames 1→5100、consumer 1280x720に縦画像がアスペクト保持のpillarboxで到達し2枚間でバー位置が変化。hostのstale→黒（Gap A）も実条件確認（`work/records/m4-u6-notes.txt`・`m4-u6-cdp3.txt`・`m4-u6-shot3*.png`、未追跡） |
| M4-u7 実カメラ・landscape（解像度切替） | 成功 | FaceTime HDカメラと1920x1080へ切替 → Stats `videoWidth:1920 videoHeight:1080`・videoFps 30・`videoBytesSent` 5605958→7570708と連続増加、rtc frames 1→900超、consumer 1280x720が生動する実カメラ映像（2枚の平均輝度0.56/0.57。`work/records/m4-u7-cdp2.txt`・`m4-u7-run2.txt`・`m4-u7-shot3/4.png`、未追跡） |
| M4-u8 AU上限超過（v1 300900 byte） | 成功 | WebCodecs/DCモード（offer `m-lines=2 codecs=opus,…`、video RTP非搭載、answer 1747 bytes）＋1080pノイズcanvasテストダブル・6Mbpsで、接続約4秒後にencoded chunk 3連続が上限超過（1758334→1039390→725137 bytes > 300900）。診断ログでdrop→ビットレート低減→IDR要求が2回、3回目で「Access unit exceeds the v1 300900-byte limit repeatedly; stop and select a lower resolution.」表示と `stop()`、以降80秒間chunk 0件。送信AU 0件のためconsumerは黒で停止と整合（`work/records/m4-u8-notes.txt`・`m4-u8-cdp.txt`・`m4-u8-run.txt`、未追跡） |
| 試験ゲート | 成功 | 各コミット前に `sh scripts/test_macos_foundation.sh` → **10/10 pass**、`sh scripts/build_macos_rtc.sh` → **12/12 pass**、`cloud` `npm test` → **17/17**、xcodebuild警告0（u1–u3時は `work/records/m4-u3-gate.txt`・`m4-u3-rtc-test.txt`・`m4-u3-xcodebuild.txt`、u4–u8時は `m4-u4-gate.txt`・`m4-u4-rtc-gate.txt`・`m4-u4-xcodebuild.txt`、本追記時は `m4-docs-gate.txt`、いずれも未追跡） |

- **環境**: macOS 26.7（Apple Silicon Mac15,10）、Xcode 26.6、Chrome（CDP 9222・fake device）。受信consumerはAVFoundation。signalingはローカル `wrangler dev`（127.0.0.1 bind）のみで実Cloudflare運用は未検証。SDPはバイト数のみ記録し、認証情報は残さない。
- **主張しない範囲**: SPS/PPS変更の単独確認、Cloudflare運用構成での確認、音声（D08）のWASAPI実マイク聴取は未実施。u6・u8は送信元にテストダブル（縦canvas・ノイズcanvas）を使い、その旨を各試験に明記する。fps以外の遅延・CPU測定は段階8へ送る。Windows側の実カメラ（OBSBOT）撮像は対話セッション1待ち。
- **段階7への判定**: 必須証拠（実ブラウザ2経路・断線復旧・Mac capture）は揃った。[07の試験項目](07-live-receiver.md)のうちカメラ切替、portrait/landscape、回線断・再接続、AU上限超過、音声無効設定はM4-u1–u8で完了し、Windows後退確認（M4-b）も段階2・段階3を同一SHA `ebd51bd` で完了した。残件のSPS/PPS変更の単独確認は段階7完了の必須項目ではなく、段階8の長期試験（RTP wrap含む）へ送る。以上からM4（段階7）の完了を判定する。

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
