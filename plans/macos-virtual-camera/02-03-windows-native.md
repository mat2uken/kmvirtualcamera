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

