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
