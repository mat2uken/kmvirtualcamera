# 段階2–3: Windows実依存と既存受信機能の確認

適用済みR01–R13をWindows製品ターゲットでビルド・動作確認する。段階7で再利用するRTC部分の不具合も先に発見する。対象は `windows/receiver`、`windows/CMakeLists.txt`、`windows/legacy_targets.cmake`。加えて `cmake/opus.cmake`、`shared/receiver`、`.github/workflows/review-foundation.yml` を確認する。[追加修正記録](../../docs/macos/09_REVIEW_FIXES.md)に各対策を記載している。

## 段階2: SDKビルドとCI

**開始条件**: Windows SDK、CMake、C++20コンパイラ、既存のnative依存を用意し、対象SHAと生成環境を記録する。ローカルMacで通った共通試験はWindows製品ビルドの代わりにならない。

1. `cmake -S windows -B build/windows -DKM_ENABLE_OPUS=ON` を実行する。Opus 1.6.1の取得・ハッシュ照合と、libdatachannel・OS APIの解決状況を確認する。offline時は検証済みのOpusソースをCMake指定する。
2. 次の製品・試験ターゲットをビルドする。コンパイル、リンク、ランタイム配置のエラーを原因ごとに直す。SDKにない型は実ヘッダーを照合する。

   ```powershell
   cmake --build build/windows --config Release --target Receiver test_h264_depacketizer test_receiver_startup test_webrtc_dtls
   ```

3. 限定したCTestを次のコマンドで実行する。全件にはカメラ登録・captureを含むため、前提と副作用を調べてから段階3で実行する。

   ```powershell
   ctest --test-dir build/windows -C Release --output-on-failure -R H264DepacketizerTest
   ```
4. 共通ライブラリとWindows転送 `.cpp` の同一実装が二重リンクされていないか、link入力を確認する。今後の共通化ではWindows側を `target_link_libraries` に寄せ、転送実装を同時に組み込まない。
5. `.github/workflows/review-foundation.yml` の3 jobを対象ブランチで実行する。Ubuntu、Windows、macOSの実行SHAと結果を記録する。現在のコミットはremoteへ未反映なので、CI確認にはブランチ公開が必要である。

**完了条件**: 上記製品ターゲットと限定CTestが同じSHAで成功し、CIの3 jobが成功する。環境依存の失敗があれば原因と再実行結果を記録する。Opus無効の共通構成も引き続き通る。実施コマンド、SDK・コンパイラ版、終了コード、ログ、実行SHAを[記録様式](verification.md)へ残す。

## 段階3: 接続・停止・音声・TURN

**開始条件**: 段階2のWindows製品ビルドが通り、試験用のブラウザ、音声endpoint、可能なら物理マイク・カメラ、UDP TURNを用意する。登録やcaptureを伴う試験は、対象端末と既存の登録状態を確認して実行する。

1. `AppController`、`PeerConnectionManager`、WinHTTP adapter、`VideoWorkerProc` の起動・Close・再初期化を実端末で試す。接続中のpacket、20ms timer、UI通知とCloseを重ね、callback gateの待機と世代切替を確認する。UI thread上のjoinが固着しないことも確認する。
2. MediaTrack H.264とWebCodecs DataChannelを別々に接続する。接続、IDR復旧、カメラ切替、解像度変更、packet loss、重複・並べ替え、無通信timeout、再接続を映像・ログ・統計で確かめる。RTP 90kHzとDC microsecondsのwrapを長時間または制御入力で確認し、host時刻と混同しない。
3. Opus RTPから48kHz/stereo/16bit PCMまでを実経路で確認し、20msで1920 **要素**を扱う箇所にframesとの混同がないか調べる。WASAPI endpoint変更、48kHz以外のmix format、underrun、ring overflow、stop/restart、実マイクの音を聞いて確認する。映像とのずれは測定値を残す。
4. WinHTTPで正常なTLS、証明書エラー、応答1MiB超過、deadline、cancel、redirect拒否を試す。資格情報やtokenはログから除く。session期限切れとOffer待機の終了状態がUIへ正しく出ることも確認する。
5. UDP TURNのrelay-only設定でcandidate pairがrelayとなることを記録する。既定libjuiceでTCP/TLSのみの設定が理由付きで失敗することを確認する。TCP/TLSが製品要件なら、実際にlibniceでbuildした構成を別に用意して試す。フラグだけで対応済みと記録しない。

**完了条件**: 映像2経路、音声、再接続、Close競合、WinHTTPの失敗経路、UDP TURN relay-onlyの結果を同じ実行SHAで記録する。失敗が残れば段階7への影響、再現手順、修正対象を明示する。Windows仮想カメラの登録・captureを行った場合は、登録だけでなく利用アプリに映像が届くことを別に確認する。

## 失敗時の切り分け

configure/build失敗は依存取得、SDK API、コンパイル、リンク、ランタイム配置に分ける。RTC接続失敗はCloudflare session、Offer/Answer、ICE candidate、DTLS、MediaTrack/DC、デコードのどこで止まったかを時系列に残す。音が出ない場合はOpus復号、PCM ring、endpoint、OS mixer、実際に聞こえる音を別々に調べる。性能改善や依存更新を、ビルド問題の解消と同時に無関係に広げない。

## 段階2 結果記録（2026-09-25）

- **結果**: 成功（製品build・限定CTest・同一SHA）。CI 3 jobのみ未実施（branchがremoteへ未公開のため。公開はユーザー判断待ち）。
- **repo / branch / HEAD / 未コミット差分**: `feature/macos-coremediaio-foundation` / 初回 `382870c` → 修正 `5a0b82f` → 最終確認 `d1b9f5f`（`d1b9f5ffcc3a1c9b79595835cae650b9b0b8f511`、2026-09-25 20:43 JST）。未コミット差分なし（`work/` は未追跡）。
- **日時 / OS / CPU / compiler**: 2026-09-25 20:15–20:46 JST、Windows 11 Pro Build 26200（物理機 NucBox_M5PLUS、Tailscale 100.127.87.30 経由のElevated SSHセッション）、AMD Ryzen 7 5825U / 32GB、Visual Studio Community 2022 17.13.35818.85、MSVC 14.43.34808。
- **Windows SDK / その他**: Windows SDK 10.0.22621.0 を選択（他に10.0.19041.0併存）、CMake 3.30.5-msvc23（VS同梱）、Git 2.50.1、Node 22.17.1、Tailscale サービスRunning。
- **artifact名 / 署名識別子**: `build/windows/Release/` の `Receiver.exe` `test_h264_depacketizer.exe` `test_receiver_startup.exe` `test_webrtc_dtls.exe`（署名なしのビルド成果物。署名は段階5以降）。
- **network構成 / ICE / TURN**: 段階2は接続試験を含まない（対象外）。
- **実行コマンド / 終了コード**:
  1. `cmake -S windows -B build/windows -DKM_ENABLE_OPUS=ON` → 0（Opus 1.6.1取得・SHA256照合、MbedTLS 3.6.2 / libdatachannel v0.22.4系 / libjuice / libsrtp / usrsctp を pins で解決、106.5秒）。
  2. `cmake --build build/windows --config Release --target Receiver test_h264_depacketizer test_receiver_startup test_webrtc_dtls` → 初回1（`test_receiver_startup` のリンクのみ失敗）→ `5a0b82f` で0（exe 4件生成）→ `d1b9f5f` 再ビルドも0。
  3. `ctest --test-dir build/windows -C Release --output-on-failure -R H264DepacketizerTest` → 0（1/1 Passed。`5a0b82f` と `d1b9f5f` で2回）。
- **期待 / 実際**: 4ターゲットのビルド成功と限定CTest通過 → 実測とも一致。
- **失敗時の再現条件 / 修正SHA / 再試験結果**: `46d5ea3`（gpu-vpp）が `pipe_publisher.h` に `D3D11VideoProcessor` を値メンバ追加した際、ソース一覧の更新は `Receiver` のみで、`pipe_publisher.cpp` を含む他6ターゲット（test_pipe_integration / test_receiver_startup / test_virtual_camera_e2e / test_vcam_pixel_fidelity / test_system_vcam_capture / test_vcam_isolated）に未解決外部参照が残った。`5a0b82f` で6ターゲットに `receiver/media/d3d11_video_processor.cpp` を追加し再ビルド成功。段階3で使う vcam 系テストも同じ原因でリンクされていた。
- **link二重化の確認（手順4）**: km共通のstatic libraryは存在せず、受信実装は `add_executable` のソースへ直接コンパイル、`target_link_libraries` は `datachannel-static`・mbedtls群・システムライブラリのみ。同一実装の二重リンクなし。
- **警告**: 製品コードのコンパイラ警告は0件（初回122件は全て第三方 `_deps`）。うち117件は自前 `windows/mbedtls_custom_config.h:3` の `MBEDTLS_SSL_DTLS_SRTP` 再定義（C4005、`legacy_targets.cmake:13` のコンパイル定義と重複）で、DTLS-SRTP設定に影響しうるため未修正の改善候補とした。自前由来のconfigure dev警告2件（トップレベルに直接の `project()` が無い／`cmake/opus.cmake` のCMP0135）は `d1b9f5f` で解消し、残る警告はpinned依存（plog/usrsctp のCMake deprecation等）のみ。
- **Opus無効の共通構成**: ルートCMakeの既定（`KM_ENABLE_OPUS=OFF`）で `sh scripts/test_macos_foundation.sh` 10/10、`sh scripts/build_macos_rtc.sh` 12/12、`cloud` `npm test` 17/17 を修正コミットごとに実行し継続通過。
- **CI 3 job（手順5）**: 未実施。`review-foundation.yml` の3 jobは公開SHAでの実行が必要。
- **ログ・実行スクリプトの保存先**: `work/win/build1.log`（初回ビルド・LNK失敗含む）、`work/win/build2.utf8.log`（警告分析）、`work/win/build3.utf8.log`（`d1b9f5f` 再ビルド・configure警告確認）、`work/win/inventory.ps1`・`work/win/build.ps1`（投入スクリプト）。

## 段階3 結果記録（2026-09-26）

- **結果**: 成功（映像2経路、音声、再接続、Close競合、WinHTTPの失敗経路、UDP TURN relay-only、TCP/TLS-only設定の理由付き失敗 の7項目を同一SHA `ebd51bd` で記録。段階3の完了条件を満たす）。
- **repo / branch / HEAD / 未コミット差分**: `feature/macos-coremediaio-foundation` / `ebd51bddd0159c3b62c4bf5c5d4a679d6d2cbe2c`（2026-09-26 00:00 JST にpush、CI `Review foundation` run 36150269346 success）。Windowsクローン `C:\Users\ku\kmvirtualcamera` もbundle同期で同一SHA。試験時は未コミット差分なし（`work/` は未追跡）。
- **日時 / OS / CPU / compiler**: 2026-09-25 23:15 – 2026-09-26 00:40 JST（TCP/TLS-only確認は00:38–00:40、実機は00:04の再起動を経て復帰後に実施）。Windows 11 Pro Build 26200（物理機 NucBox_M5PLUS、Tailscale 100.127.87.30 経由Elevated SSH）、AMD Ryzen 7 5825U / 32GB、MSVC 14.43.34808、VS同梱CMake 3.30.5、Node 22.17.1 / npm 10.9.2。Mac側（TURN・worker host）: macOS、Homebrew coturn 4.18.0、wrangler 3系（cloud/node_modules）。
- **artifact名 / 署名識別子**: `build/windows/Release/Receiver.exe` と `test_win_http_paths.exe`（署名なしのビルド成果物。署名は段階5以降）。
- **network構成 / ICE candidate種別 / TURN backend**:
  - 映像2経路・音声・再接続・Close競合: デプロイ済みCloudflare Worker signaling（`https://webrtc-bridge-signaling.mat2uken.workers.dev`）、tailnet direct（host / srflx候補）、`icePolicy=all usableTurn=0`。
  - UDP TURN relay-only: Windows上のローカル wrangler dev（`http://127.0.0.1:8787`、`Receiver.exe --url` でloopback指定。`cloud/.dev.vars` はgitignore済み）が `ICE_TRANSPORT_POLICY=relay` + `TURN_STATIC_URL=turn:100.83.174.3:3478` をrtcConfigurationへ供給、TURN backendはMacのcoturn 4.18.0（100.83.174.3:3478 UDP、lt-cred-mech静的資格情報、relay 49152–65535。Macアプリファイアウォール無効）。
  - TCP/TLS-only（理由付き失敗確認）: 同じローカル wrangler dev に `.dev.vars` を `TURN_STATIC_URL=turns:100.83.174.3:5349` + `ICE_TRANSPORT_POLICY=relay` へ差し替え（wrangler再起動で反映、`show-rtc-config.ps1` で `policy=relay` と `turns:` user=kmturn credLen=18 を確認）。
- **実行コマンド / 終了コード**:
  1. 共通gate: `sh scripts/test_macos_foundation.sh` → 0（10/10）、`sh scripts/build_macos_rtc.sh` → 0（12/12）、`cloud` `npm test` → 0（18/18。static TURN単体テストを追加し17→18）。
  2. `ctest --test-dir build/windows -C Release --output-on-failure -R WinHttpPathsTest` → 0（Passed 23.36s、100%）。
  3. `powershell -File close-cycle.ps1`（mid-stream WM_CLOSE）→ 正常終了 ×3回（1.0s / 0.5s / 1.0s で shutdown complete・process-gone）。
  4. `powershell -File close-cycle.ps1 -NoClose` + `drive-and-flag.ps1 mt`（再接続）→ 正常終了。
  5. `powershell -File close-cycle.ps1 -BaseUrl http://127.0.0.1:8787` + `drive-and-flag.ps1 mt`（relay-only）→ 正常終了（WM_CLOSEも1.0sで完了）。
  6. `powershell -File close-cycle.ps1 -BaseUrl http://127.0.0.1:8787` + `drive-and-flag.ps1 mt`（`.dev.vars` を `turns:` + relay へ差し替え・wrangler再起動後。Chromeは同一sshセッション内で `start-chrome.ps1` によりCDP 9222起動）→ ドライバー正常終了（`CONNECT_TIMEOUT +41s`、予定どおりの失敗）、WM_CLOSEも0.5sで完了。
  7. `ctest --test-dir build/windows -C Release --output-on-failure -R WinHttpPathsTest` → 0（Passed 23.24s、100%。HEAD `ebd51bd` で再実行し同一SHA証跡に）。
  8. Windowsビルド: `cmake --build build/windows --config Release`（build9–11.log、製品警告0件）。
- **期待した映像・音・状態 / 実際の観測**:
  1. **映像2経路**: MediaTrack H.264とWebCodecs DCを別々に接続 → 両経路で offer/answer・state 1→2→3→5、受信デコード（`[H264Decoder] Configured format` と `[APP] video worker decoded=90`）、ABR中のSTREAM_CHANGE再設定（320x240→480x360→640x480）も成功。スクリーンショットA/B/C と stats を `work/win/records/` に保存（段階2記録のBUILD6実行を含む）。
  2. **音声**: Opus RTP→48kHz PCM実経路 — `[RTC] audio rtp packets=500`、`[MEDIA] audio callbacks=500 samples=960000`（20ms×500回=10秒分）、renderer on。WASAPI endpoint切替・実マイク聴取は未実施（対話セッション1待ち、段階7判定への影響なし＝受信側PCM経路は実測済み）。RTP 90kHz / DC microsecondsのwrapは未実施（段階8の長時間試験で確認、時刻系の混同なし）。
  3. **再接続**: 前回close正常終了後のReceiver再起動 → 新セッション・新joinUrl → 接続 → stop で `state=3`→`state=5` クリーン（音声500 packets、decoded=90、ABR再設定を含む）。
  4. **Close競合**: 接続中（packet・20ms timer・UI通知の稼働時）へmid-stream WM_CLOSE → `[APP] shutdown begin` → `signaling-joined` → `gate-waited`/`timer-joined`/`peer-done` → `rtc-closed` → `video-joined`（decoder released 0–16ms）→ `output-joined` → `media-stopped` → `shutdown complete`、wm-close→complete 1.0s/0.5s/1.0s、process-gone同値。UI thread上のjoin固着なしを確認。**試験中に欠陥1件を発見・修正**: H264 decoderがMFT提供の出力サンプルを解放しておらず出力プール枯渇で `ProcessOutput` が無限待機（watchdog `video step=30 decoder stage=120` で断定、`output stream flags=0x107 providesSamples=1` が提供パスであることを確認）→ `c5dee20` で所有権取得・都度解放に修正、修正後2回連続でクリーン終了。
  5. **WinHTTP失敗経路**: `HTTP response too large`（1MiB超過・大redirect）、`status=302`（redirect拒否）、`HTTP deadline exceeded`（9–30秒）、`HTTP request cancelled`、`HTTP send/receive failed`（expired.badssl.com 経由の証明書・送受信失敗）、`signaling requires HTTPS (except explicit loopback development)`（loopback以外の平文拒否）、正常系201（失敗なし）をstderr文字列でassert → Passed。資格情報・tokenはログに含めない。
  6. **UDP TURN relay-only**: ブラウザgetStatsのcandidate pairが local=relay / remote=relay、`state=succeeded` `nominated=true`（2回のstats観測で一致）。候補収集は「2個」のみ＝relay専用ポリシーが有効。640x480 19–20fps・音声をrelay経由で継続送出、rtt 4–13ms、receiver側も `icePolicy=relay` で初期化し同サイクルのmid-stream closeも1.0sで完了。
  7. **TCP/TLS-only設定の理由付き失敗**（`work/win/records/turn-tcp-tls-only-fail.txt`、HEAD `ebd51bd`、2026-09-26 00:38–00:40 JST）:
     - rtcConfiguration確認（`show-rtc-config.ps1`）: `policy=relay`、`urls=turns:100.83.174.3:5349 user=kmturn credLen=18`（wrangler再起動で `.dev.vars` 反映を確認）。
     - ブラウザ送信側: offer送信（6142 bytes）前に `ICE Candidate Error: 701 Failed to establish connection (turns:100.83.174.3:5349?transport=tcp)` を複数回、`ICE収集完了、0 個の候補を収集`、driverは `CONNECT_TIMEOUT +41s`（stats `packets:{}`）。
     - レシーバー側: `[SIG] offer received bytes=6142` → `[RTC] TURN TCP/TLS skipped: this build uses libjuice. Use a UDP TURN endpoint or a matching libnice build.` → `[RTC] Initialize failed: relay policy has no supported TURN endpoint`（例外は `[APP]` 未到達でRTC初期化内で捕捉、以降answerなし＝接続不成立）。
     - **フラグだけで対応済みとせず、理由（libjuiceはTURN TCP/TLS非対応）付きで失敗することを実測で確認**。D11（`KM_TURN_TCP_TLS=0`）の既知制約と整合。
- **fps / 遅延 / queue深さ / drop / CPU・GPU / memory**: relay経由で rtt 4–13ms、videoFps 19–20（640x480、fake device）。decoderは90フレーム毎の心跳ログで稼働確認、close時 `decoder released ms=0–16`。長期・queue深さ・drop集計は未測定（段階8）。
- **CI 3 job**: `Review foundation` run 36150269346（HEAD `ebd51bd`）success。段階2の382870c以降、公開SHAでの3 job成功を継続確認。
- **ログ・動画・画像の保存先**: `work/win/records/close-cycle-build11-clean1.txt`・`close-cycle-build11-clean2.txt`（Close競合2回）、`work/win/records/reconnect-build11.txt`（再接続）、`work/win/records/turn-relay-only.txt`（relay-only）、`work/win/records/turn-tcp-tls-only-fail.txt`（TCP/TLS-only理由付き失敗、HEAD `ebd51bd` で収集）、`work/win/records/winhttp-paths-ctest-ebd51bd.txt`（ctest同一SHA再実行）、`work/win/records/` の A/B/C スクリーンショット、`work/win/build9–11.log`（ビルド）、Mac `~/turnserver-kmtest_2026-09-25.log`（coturn、repo外）。
- **失敗時の再現条件 / 修正SHA / 再試験結果**: mid-stream WM_CLOSEで `video step=30 decoder stage=120` が50秒間固定（ProcessOutput無限待機）→ `c5dee20`（提供サンプルの `Attach` とループ毎Release）で修正。修正前は3回連続でshutdown hang、修正後は2回連続で complete/process-gone 1秒以内。watchdogとstep/stageカウンタは再発監視のため残置。
- **段階7への影響**: なし（段階3の完了条件7項目を同一SHA `ebd51bd` で充足）。TCP/TLS-onlyの失敗はフラグ起因ではなくlibjuice非対応という理由付きで実測、UDP TURN relay-onlyの成功と対で確認済み。残る未実施（RTP wrapの長期試験＝段階8、実マイク/WASAPI聴取＝対話セッション1）は段階7の判定に影響しない。


## A3（ReceiverEngine切替）後の段階3相当回帰（2026-09-26）

- **結果**: 成功。要求範囲の映像2経路とmid-stream Close競合を同一SHA `6cfd7c6` で再確認した。
- **同時に確認した項目**: 両回帰とも `ICE_TRANSPORT_POLICY=relay` で relay↔relay が成立した。Opus RTPも受信した。
- **repo / branch**: `feature/macos-coremediaio-foundation`（push済み）。`work/` は未追跡。
- **HEAD**: `6cfd7c64848b894a026b8d4e1be168fbfdffb7a3`。
- **同期**: Windowsクローン `C:\Users\ku\kmvirtualcamera` はbundle同期で同一SHA・clean。
- **変更内容（A3）**: pcmのvideo経路を `km::engine::ReceiverEngine` に置換した。payload型の絞り込み、SSRC unwrap、RTP/DC仲裁、キーフレームの50msスロットルと経路選択をengine側へ移した。フレーム配布とcontrol送信もengine側に移し、bandwidth estimator・Opus・RTCP flushはpcm側に残した。RTPの二重パースは許容方針どおり行う。
- **配線（Windows）**: pcmを含む3ターゲットへengineの `.cpp` を追加した。ターゲットは Receiver、test_receiver_startup、test_webrtc_dtls である。
- **配線（Mac）**: ルート `CMakeLists.txt` の `km_rtc_shared` は `km_receiver_engine` をlinkする。`macos/project.yml` のOTHER_LDFLAGSに `-lkm_receiver_engine` を追記した。project.pbxprojはxcodegenで再生成した。Windows転送 `.cpp` と共通ライブラリの二重リンクはない。
- **gate（最終tree）**: `sh scripts/test_macos_foundation.sh` → 0（10/10・警告0）。`sh scripts/build_macos_rtc.sh` → 0（12/12・警告0）。`cloud` `npm test` → 0（18/18）。ログは `work/records/a3-gate-*.txt`（未追跡）。
- **Windowsビルド・全ctest**: `cmake --build build/windows --config Release` は製品コード警告0件。`ctest -C Release` は15/16だった。`VirtualCameraSmoothnessTest` のみ環境起因で既存失敗する。証跡は `work/win/records/a3-build-ctest-6cfd7c6.txt`。
- **実機回帰（要求2経路）**: `work/win/run-regression.ps1` を使う。wrangler dev・Chrome CDP 9222・drive-and-flag・close-cycleを同一sshセッション内で起動する手順である。
  1. **MediaTrack/RTP**（`-Mode mt`）: `CONNECTED +10s`。getStatsは `local=relay / remote=relay / state=succeeded / nominated=true`。送信は640x480@20fpsだった。受信側は `[RTC] state=2` と `[MEDIA] video frames=90 bytes=202681 gen=2` を記録した。`[APP] video worker decoded=90` と `[MEDIA] audio callbacks=250 samples=480000` も出た。ストリーム中のWM_CLOSEは0.5sで shutdown-complete / process-gone に到達した。終了コードは0（`work/win/records/a3-regression-mt.txt`）。
  2. **WebCodecs/DC**（`-Mode dc`）: `CONNECTED +10s`、relay↔relay。`dc-km-video-stream` はopenでsent 294522 bytes / 306 msgs。`dc-km-control` は双方向（sent 213 / recv 201）。受信側は `[RTC] datachannel open: video` と `control` を記録した。`[MEDIA] video frames=90 bytes=191658 gen=2`、`decoded=90`。WM_CLOSEは0.5s、終了コード0（`work/win/records/a3-regression-dc.txt`）。
- **環境の是正（失敗試行の証跡）**: 当日、ローカルwranglerの `cloud/.dev.vars` は段階3のTCP/TLS-only確認用の設定のまま残っていた。`TURN_STATIC_URL=turns:100.83.174.3:5349` の資格情報はcoturn設定と不一致だった。coturnログは `user kmturn credentials are incorrect` を出した。受信側は `Initialize failed: relay policy has no supported TURN endpoint` で止まった。ブラウザ側は `TURN allocate request timed out` で候補0個だった。
- **是正内容**: `TURN_STATIC_URL` を `turn:100.83.174.3:3478`（Mac coturn 4.18.0 UDP、firewall disabled）へ戻した。STUN Bindingの応答をWindowsからMacで実測して到達を確かめた。`TURN_STATIC_CREDENTIAL` をcoturn設定値に一致させた（値はログ・リポジトリに書かない）。以後は relay↔relay が成立した。①driver先行起動時の旧 `join.txt` 取得は事前削除で解消した。②wranglerのssh切断による停止は同一sshセッション内への集約で解消した。
- **既存失敗の切り分け**: `VirtualCameraSmoothnessTest` は `MFEnumDeviceSources` が "WebRTC Bridge" を列挙できない環境要因で失敗する。対象コード（pcm）には依存しない。`git stash` でA3差分を外したtreeで同じターゲットを再ビルド・再実行しても同じ失敗になった。`test_system_vcam_capture` はStartVirtualCamera後に直接COM経由で成功した。
- **未再実施（本回帰の対象外）**: 再接続、WinHTTP失敗経路、TCP/TLS-only設定の理由付き失敗は段階3（`ebd51bd`）の記録を維持する（A3は未変更箇所）。RTP wrap・AU上限の長期試験は段階8へ送る。実マイク/WASAPI聴取は対話セッション1へ送る。
