# 段階9–10: 音声、配布、資料更新

映像の初回到達点は段階7、安定化の判定は段階8で行う。この文書は、音声を製品へ含める判断と、署名済みアプリの配布判断に必要な残作業を管理する。音声は同じ版でCore Audio出力まで含める（C4）。製品対応OS・CPU、配布方式は現時点で未決定である。

## 段階9: 音声

**判断条件**: 映像を先に公開するか、音声も同じ版に含めるかを製品要件として決める。WindowsのOpus復号・WASAPI実測と、Macの音声利用先を確認する。Camera Extensionだけでは仮想マイクを提供できない。

1. ブラウザOpusと `shared/receiver/audio/opus_rtp_decoder.*` をMacへ接続する。48kHz/stereo PCMの要素数とframe数、PLC、再接続時resetを実経路で確かめる。動画との時刻差と遅延を測る。
2. Core Audio出力が要件なら、専用audio workerから選択した出力deviceへbounded PCM queueを渡す。device変更、48kHz以外の実endpoint、underrun/overflow、ミュート、停止・再開を実音で確認する。RTC callbackを音声deviceの待機で止めない。
3. 仮想マイクが要件なら、使用するmacOSの音声device方式を別途調査・設計し、署名、導入、複数アプリからの利用、削除を映像Extensionとは別に検証する。方式が決まるまでCamera Extensionのsink/sourceを仮想マイクと呼ばない。

**完了条件**: 採用する音声範囲を記録する。採用する機能は、実ブラウザ入力からOS出力または仮想マイク利用アプリまで、音質・映像とのずれ・停止復帰を実測で示す。採用しない機能は公開仕様とUIに含めない。

### 段階9 開始時記録（実測）

| 項目 | 値 |
|---|---|
| repo | `kmvirtualcamera-macos-coremediaio-foundation` |
| branch | `feature/macos-coremediaio-foundation` |
| HEAD | `272f787`（段階8 S6の記録コミット） |
| 未コミット差分 | `work/`（未追跡）のみ |
| OS / CPU | macOS 26.7 (25G229) / Apple M3 Max (arm64) |
| Xcode / SDK | Xcode 26.6 (17F113) / macOS SDK 26.5 |
| compiler / CMake | Apple clang 21.0.0 / CMake 4.3.4 |
| ブラウザ / 送信 | Chrome 153.0.8010.53、CDP 9222 |
| Macの音声経路 | 実装なし。hostのaudio callbackは空 |
| 決定待ち | 音声の製品範囲は未決定（C4） |

着手前の調査で、macOS側の音声データ経路は無いことを確認した。hostは `macos/host/app_delegate.mm` の空callbackへPCMを渡すだけで、Core Audioへの書き出しは無い。`macos/project.yml` のリンクに `-lopus` も無い。

`build/macos-rtc/CMakeCache.txt` の `KM_ENABLE_OPUS` は `OFF` である。オーディオm-lineの受理は `peer_connection_manager.cpp` の設定失敗で拒否される。

Windows側のOpus復号とWASAPI実測は段階3で済み、共通の `opus_rtp_decoder.*` は両OSで同じ実装を使う。

この表のHEAD・差分・環境を単位を始めるたびに更新し、[記録様式](verification.md)に試験結果を紐付ける。

### 段階9 実施単位

| 単位 | 内容 | 到達点 |
|---|---|---|
| A1 | Opus無効の現状で音声付き送信 | audio m-lineと拒否理由の実測 |
| A2 | Macの `KM_ENABLE_OPUS=ON` ビルド | libopus取得とリンクの可否 |
| A3 | 実経路のPCM計測 | 要素数・frames・channels・rate |
| A4 | PLCと再接続時reset | 欠損と再接続の挙動 |
| A5 | 音声と映像の時刻差 | 動画とのずれの実測値 |
| A6 | Core Audio出力 | 採用。実装と実音で手順2を満たす |
| A7 | 仮想マイク方式 | 対象外（C4で含めない）。手順3は行わない |

A1からA5は音声の製品範囲が未決定でも、判断材料になる実測として進める。C4の決定後はA6を着手し、A7は公開仕様とUIに含めない。

### 段階9 C4決定（2026-09-28）

| 項目 | 内容 |
|---|---|
| 決定 | 同じ版で音声（Core Audio出力まで） |
| 対象外 | 仮想マイク（A7）。Camera Extensionは映像のみ |
| 手順 | 手順2を実装・実音で確認する。手順3は行わない |
| 完了条件への効き方 | 音質・映像とのずれ・停止復帰を実音で実測する |
| 未決定のまま | 製品対応OS・CPU、配布方式 |
| 出典 | 2026-09-28の確認。回答は「同じ版で音声（A6まで）」 |

この決定により、段階10へはA6の実測を添えて進む。A7を含めない点は公開仕様とUIにも反映する。

## 結果記録

### A1〜A5 実行環境

| 項目 | 内容 |
|---|---|
| repo | `kmvirtualcamera-macos-coremediaio-foundation` |
| branch | `feature/macos-coremediaio-foundation` |
| HEAD | 計測開始 `272f787`、Opus設定 `026955b`、本記録は次 |
| 未コミット差分 | `work/`（未追跡）と本記録のみ |
| OS / CPU | macOS 26.7 (25G229) / Apple M3 Max (arm64) |
| Xcode・SDK | Xcode 26.6 (17F113) / macOS SDK 26.5 |
| compiler / CMake | Apple clang 21.0.0 / CMake 4.3.4 |
| ブラウザ | Chrome 153.0.8010.53（CDP 9222、fake device） |
| network | `wrangler dev` 127.0.0.1:8787、ローカル `turnserver` |
| 計測構成 | Debug、CDP送信、hostと受信側の臨時計測 |

臨時計測は `macos/host/app_delegate.mm` と `shared/receiver/audio/opus_rtp_decoder.cpp` に置き、各計測後に撤去した。撤去後の再ビルドで該当文字列は0件である。ログは `work/records/` に原本を残す。

### A1 Opus無効の現状で音声付き送信

| 項目 | 内容 |
|---|---|
| 段階・試験ID | 段階9 A1（Opus無効で音声付き送信） |
| 目的・合格条件 | audio m-lineの扱いと拒否理由を実測で残す |
| 結果 | 成功（audio m-line拒否、`audioRows` 0、理由2行） |
| 実行 | `node work/records/a1-cdp.mjs <join-url>` → 0 |
| ログ | `a1-host.log`、`a1-run.txt`、`a1-sender-stats.txt` |

hostのofferは `m-lines=2` で先頭がopusである。hostは `offer m-line rejected: audio` と `Opus audio unavailable opusPt=111; audio track rejected` の2行を出す。

送信側診断は Stats 行6件・total 98・`audioRows` 0 で、映像は 1280x720・29〜30fps・`videoBytesSent` が増加する。

### A2 `KM_ENABLE_OPUS=ON` のビルド

| 項目 | 内容 |
|---|---|
| 段階・試験ID | 段階9 A2（MacのOpusビルドとリンク） |
| 目的・合格条件 | libopus取得・リンクと全件試験の通過 |
| 結果 | 成功（libopus.a 1,869,624 bytes、ctest 13/13） |
| ログ | `work/records/09-gate-rtc.txt`、`09-gate-xcodebuild.txt` |

`build/macos-rtc/CMakeCache.txt` の `KM_ENABLE_OPUS` は `ON`、取得物は `_deps/km_opus-build/libopus.a` である。

`macos/project.yml` の `OTHER_LDFLAGS` に `-lopus`、`LIBRARY_SEARCH_PATHS` に `km_opus-build` を追加し、pbxprojへ反映した。`scripts/build_macos_rtc.sh` と `scripts/build_macos_xcode_deps.sh` に `-DKM_ENABLE_OPUS=ON` を付けた。

`ctest` は13/13で、`opus_decode` が1件増える。`xcodebuild` は BUILD SUCCEEDED・警告0である。

### A3 実経路のPCM計測

| 項目 | 内容 |
|---|---|
| 段階・試験ID | 段階9 A3（実経路のPCM計測） |
| 目的・合格条件 | 要素数・frames・channels・rateと件数を残す |
| 結果 | 成功（60秒で n=3000、1920要素以外0件） |
| ログ | `work/records/a3b-host.log`、`a3b-run.txt` |

`m-line rejected` と `Opus audio unavailable` は0件で、`rtc state=2` に到達する。A1と同じoffer形式から受理が変わった。

callbackは `elements=1920 frames=960 channels=2 rate=48000` である。n=1000・2000・3000の時刻差はいずれも20.0秒で、50 callback/s になる。

要素数が1920でないcallbackは0件、`gapMs>40` も0件である。計測の下限は初期値0のままなので、下限は主張しない。

### A4 PLCと再接続時reset

| 項目 | 内容 |
|---|---|
| 段階・試験ID | 段階9 A4（欠損と再接続の挙動） |
| 目的・合格条件 | 欠損時と再接続時の受信側挙動を実測で残す |
| 結果 | 成功（停止・再開は5回とも復帰、分岐到達0） |
| ログ | `a4c`〜`a4n` の `*-host.log` と `*-run.txt` |

| 試行 | 操作 | host側の結果 | 証跡 |
|---|---|---|---|
| mute | `enabled=false` 3回・区間1秒 | callback停止なし、最大 gapMs 52 | `a4c`〜`a4g-host` |
| stop | `replaceTrack(null)` 40/100/140/300ms | 停止 67/101/150/311ms | `a4k-run`・`a4k-host` |
| stop計測 | 同 40/100/140ms | `gap=0` 11件、分岐到達0 | `a4m-host` |
| cycle | 送信15秒・停止5秒を5回 | 停止13050〜14081ms、再開後1920 | `a4n-host` |

muteは送信側 Stats が2秒間隔で、ミュート1秒の区間では bytes の停止を判定できない。受信側で停止がないことから、`enabled=false` はRTPを止めない。

`replaceTrack(null)` のdetach区間で、送信側 bytes は `20716 / 390 packets` のまま進まない。

再attach後は `21126 / 395 packets` へ5パケット増える（`a4h-run`）。

RTP時刻の分岐計測は `a4m` が `gap=0` 11件・`no-endts` 1件、`a4n` が同48件・同6件である。

`branch=`2種・`reset-`2種・`recreate-`2種は、2試行とも0件である。

再接続5回では callback が13050〜14081ms止まり、再開後は `elements=1920`、nは 799→800、1597→1598 と連続する。接続ごとに `no-endts` が1件出る。

ブラウザ送信は停止後もRTP時刻を連続に保つため、送信側の停止は時刻ギャップではなくcallback間隔として現れる。PLC分岐は実経路で到達しない。

PLCそのものは共通単体試験で担保する。`tests/shared/test_opus.cpp` の欠損1件（20ms）では PLC 960フレームが出る。この1件は ctest 13/13 が通る。

### A5 音声と映像の時刻差

| 項目 | 内容 |
|---|---|
| 段階・試験ID | 段階9 A5（映像との到着差） |
| 目的・合格条件 | 音声callbackと映像AUの到着差を実測で残す |
| 結果 | 成功（vAge 36件 mean 19.4ms、aAge 38件 mean 20.0ms） |
| ログ | `work/records/a5*-host.log`・`a5*-run.txt`・`a5-tail.mjs` |

| 区分 | n | min | p50 | mean | max | 証跡 |
|---|---|---|---|---|---|---|
| vAge 音声→最新映像 | 36 | 0 | 17 | 19.4 | 33 | `a5b-host` |
| aAge 映像→最新音声 | 38 | 0 | 14 | 20.0 | 76 | `a5b-host` |
| vAge 別試行60秒 | 30 | 0 | 21 | 19.9 | 48 | `a5-host` |

vAgeは音声callback時刻から最新映像AUまでの経過、aAgeは映像AUから最新音声callbackまでの経過である。単位はms。

75秒の試行では開始直後の vAge 211ms 1件と、送信終了後の aAge 100ms超217件を除いた値である。映像30fpsの周期33.3msに対し、vAgeの最大は33msで収まる。

音声callback間隔は n=1000ごとに20.0秒である。稼働中の `gapMs>40` は a5b が1件45ms、a5 が0件である。

送信スクリプト終了の時刻に、送信側 audio track が `ended` になる。video track は `live`、PCは `connected` のままで映像AUが続く（2試行とも同じ）。

CDP照会の `work/records/a5-tail.mjs` は audio `ready=ended`・video `ready=live`・`connectionState=connected` を返す。原因は未調査である。A5の値は接続中の区間だけの値になる。

この差は到着時刻の差であり、音声と映像のメディア時計の差ではない。時計の差は送信側の時刻を受信側で突き合わせる計測が別途必要である。

#### A5-b 受信側の遅延（60秒）

| 項目 | 内容 |
|---|---|
| 目的・合格条件 | 受信側で触れる音声・映像の遅延を実測で残す |
| 結果 | 成功（音声 mean 26.8ms、映像 mean 3.3ms） |
| ログ | `work/records/a5c-host.log`・`a5c-run.txt` |

| 区間 | n | min | mean | max |
|---|---|---|---|---|
| 音声 到着→PCM callback | 3000 | 20.0ms | 26.8ms | 50.8ms |
| 映像 submit→publish | 1746 | — | 3.3ms | 238.2ms |

音声は `OpusRtpDecoder::Receive` の到着から PCM callback までの時間である。下限20.0msは `Tick` の20ms待ちと一致する。

max 50.8ms は最初の3件で出て、残り2997件でこれを超える値は無い。

映像は `VideoPipeline::submit` から sink publish までで、mean 3.3ms である。max 238.2ms は接続開始時の値である。

2つは異なる区間である。音声出力が未実装のため、出力時の映像とのずれはまだ測れない。

### A6 Core Audio出力

| 項目 | 内容 |
|---|---|
| 段階・試験ID | 段階9 A6（Core Audio出力） |
| 目的・合格条件 | RTC callbackがdevice待ちをしないbounded queue経由の出力を実音で残す |
| 結果 | 成功（定常区間 underrun=0、device rate比0.9997） |
| 実装 | `macos/receiver/audio_pcm_queue.h`・`audio_output.cpp` |
| ログ | `work/records/09-a6-device-test.txt`・`a6-host.log`・`a6-run.txt` |

実装は3点に分ける。`AudioPcmQueue` は bounded PCM queue で、overflowはoldestを落とし、underrunはempty popを数える。`AudioOutput` は専用workerがAudioQueueを所有し、create/prime/start/device変更/disposeを担当する。RTC callbackは `push` のみでdevice呼び出しをしない。

workerは `AudioQueueStop`/`Dispose` を状態lockの外で呼ぶ。これらは完了callbackを待ち、callbackが同じlockを取るため、lock内ではデッドロックする。完了callbackは現queue以外のbufferを捨てる。

#### A6-a 実endpointでの動作（device test）

| 区間 | wall | rate比 | underrun | dropped | 証跡 |
|---|---|---|---|---|---|
| steady 4.0s | 4.00 | 0.9939 | 0 | 0 | `09-a6-device-test` |
| muted 2.0s | 2.00 | 1.0086 | 0 | 0 | 同上 |
| unmute 1.0s | 1.00 | 0.9962 | 0 | 0 | 同上 |
| burst 500ms一括 | — | — | 0 | 48000 | 同上 |
| gap 1.5s | 1.51 | — | 73 | 0 | 同上 |
| restart後 1.5s | 1.50 | 0.9854 | 0 | 0 | 同上 |
| device変更 2.0s | 2.00 | 0.9889 | 0 | 0 | 同上 |
| 44.1kHz endpoint 3.0s | 3.00 | 0.9924 | 0 | 0 | 同上 |

rate比は device が再生したbuffer数×20ms / wall である。1.0なら client frame のまま実時間で再生される。44.1kHz endpoint は client format 48kHz のまま device rate を44100に変え、rate比0.9924で AudioQueue が変換した。rate は48000へ戻した。

device変更は `kAudioQueueProperty_CurrentDevice` に device UID を渡す。AudioDeviceID を渡すと `kAudioQueueErr_InvalidDevice` (-66683) になる。

全checkは通過した。1kHz tone を選択した endpoint へ再生する。

#### A6-b 実経路E2E（browser→RTC→device）

| 項目 | 値 |
|---|---|
| session | 08:08:34 作成、device=89 (MacBook Proのスピーカー) 48kHz |
| 定常区間 | 08:09:02→08:09:38（36秒） |
| 定常 underrun | 0（depth 3840〜5760 を維持） |
| 音声 pushed | 2972160 elements（61.9秒分）= popped |
| device playSec/wallSec | 0.9997（実時間で消費） |
| pushMaxUs | 462.5（RTC callbackの最大待ち） |
| dropped | 0 |
| underrun | 2186（queue が空になった回数。ライブ音声パイプラインでは正常） |
| 映像 | accepted=1065 published=1064 backpressure=0（影響なし） |

session作成からRTC接続まで24秒あり、その間は音源なしでdeviceが無音を再生する（underrun 50/s）。接続後の最初の4秒は queue が空からの再建で underrun 113件だった。その後は36秒間 underrun 0 である。最終的に device は 61.9秒分の PCM を実時間で消費した。

track が ended になった後は再び underrun 50/s になる（A5と同じ現象）。session 開始時に device 名が空文字だったが、現在は同じ device で名が取得できるため起動直後の一時的な値とみなす。

### ゲート（計測後のtree）

| ゲート | 結果 | 証跡 |
|---|---|---|
| `sh scripts/test_macos_foundation.sh` | 10/10 pass、警告0 | `work/records/09b-gate-foundation.txt` |
| `sh scripts/build_macos_rtc.sh` | 13/13 pass、資材のCMake警告4件 | `work/records/09b-gate-rtc.txt` |
| `cloud` `npm test` | 18/18 pass | `work/records/09b-gate-cloud.txt` |
| `xcodebuild` Debug | BUILD SUCCEEDED、警告0 | `work/records/09b-gate-xcodebuild.txt` |

3ゲートとxcodebuildは臨時計測を外したtreeで終了コード0である。rtcの警告4件は pinned 資材の CMake deprecation で、自社コードは0件である。

A5までのtreeでも同じ4件を通しており、証跡は `work/records/09-gate-*.txt` である。

### ゲート（A6計測後のtree）

| ゲート | 結果 | 証跡 |
|---|---|---|
| `sh scripts/test_macos_foundation.sh` | 12/12 pass、警告0 | `work/records/09c-gate-foundation.txt` |
| `sh scripts/build_macos_rtc.sh` | 15/15 pass、警告0 | `work/records/09c-gate-rtc.txt` |
| `cloud` `npm test` | 18/18 pass | `work/records/09c-gate-cloud.txt` |
| `xcodebuild` Debug | BUILD SUCCEEDED、警告0 | `work/records/09c-gate-xcodebuild.txt` |

A6の新規test2件が両treeで通った。rtc 15件は既存13件へ音声2件を足した数である。rtcのdeprecation警告は再configureが無く0件だった。

C4は「同じ版で音声（Core Audio出力まで）」で決まり、A6を実施した。A7は対象外である。A1からA5の値は、A6の比較基準として残す。

### 段階10 C1決定（2026-09-28）

| 項目 | 値 |
|---|---|
| Team ID | `K7VNGA9K78` |
| host bundle ID | `jp.yasagure.kmvirtualcamera.macos` |
| extension bundle ID | `jp.yasagure.kmvirtualcamera.macos.camera-extension` |
| App Group | 既存の `group.jp.yasagure.kmvirtualcamera.k7vnga9k78` を沿用（新規不要） |
| 配布形式 | App Store、macOS/Windows |
| Intel対応 | 不要 |
| 署名 | 開発署名（Apple Development） |

App Group は bundle ID と独立しており、team prefix だけで決まる。開発署名ならXcodeの自動プロビジョニングがビルド時に作成するため、ポータルでの新規作成は不要である。

### A7 署名（開発用、段階10 item1）

| 項目 | 内容 |
|---|---|
| 目的・合格条件 | 新bundle IDでhost・Extensionが署名され、entitlementsが実生成物で確認できる |
| 結果 | 成功（host/extension とも bundle ID・team・App Group・署名が一致） |
| ログ | `work/records/10a-gate-xcodebuild.txt`・`10a-gate-foundation.txt` |

変更は3ファイルである。`macos/project.yml` は bundleIdPrefix と各 PRODUCT_BUNDLE_IDENTIFIER を直す。`app_delegate.mm` は extension 識別子を直す。`ids.h` は host の AMFI 要求と署名識別子を直す。App Group は変更なし。

| 確認対象 | 値 |
|---|---|
| host bundle ID | `jp.yasagure.kmvirtualcamera.macos` |
| extension bundle ID | `jp.yasagure.kmvirtualcamera.macos.camera-extension` |
| host team identifier | `K7VNGA9K78` |
| extension team identifier | `K7VNGA9K78` |
| App Group（両者） | `group.jp.yasagure.kmvirtualcamera.k7vnga9k78` |
| 署名 | Apple Development: Kenichi Matsumoto (M4FBGCLF45) |

`codesign --verify --deep --strict` はhost・extensionともに通り、entitlementsのteam-identifierとApp Groupが一致した。

AMFI要求は host の実 bundle ID・team と一致する。文字列は `identifier "jp.yasagure.kmvirtualcamera.macos" and certificate leaf[subject.OU] = K7VNGA9K78`。

| ゲート | 結果 | 証跡 |
|---|---|---|
| `sh scripts/test_macos_foundation.sh` | 12/12 pass、警告0 | `work/records/10a-gate-foundation.txt` |
| `xcodebuild` Debug（`-allowProvisioningUpdates`） | BUILD SUCCEEDED、警告0 | `work/records/10a-gate-xcodebuild.txt` |

`-allowProvisioningUpdates` は新bundle IDのApp IDとprofileの自動作成に必要である。

## 段階10: 署名・更新・配布

**開始条件**: 段階8の品質判定が済み、配布対象OS・CPU・音声範囲・Team ID・bundle ID・App Group・配布経路を確定する。開発用署名での動作と、配布物の導入結果を分ける。

1. host、Camera Extension、埋め込むnative依存の署名とentitlementsを実際の生成物で確認する。hostとExtensionのTeam・識別子・App Groupを一致させ、不要な権限を除く。資格情報、署名秘密鍵、TURN credentialをrepo・ログ・配布物へ混入させない。
2. 配布経路に必要なnotarization、stapling、パッケージ化を実施する。新規Macの通常設定で `/Applications` 導入から一般アプリのcaptureまで再確認する。提出成功だけを実機合格にしない。
3. 更新、既存版からの置換、deactivation、アンインストール、再起動要求、残留したdeviceの有無を試す。hostが消えた後のExtension状態、再インストール時のstable IDと利用アプリの選択状態を記録する。
4. mbedTLS、libdatachannel、libopus、npm依存と推移的依存を、配布時点の脆弱性情報・ライセンス・実際の同梱版で点検する。`npm ci` 時点のauditには15件の警告があったため、個別に影響、更新可否、対処を記録する。依存の大型更新は対応範囲と回帰結果を分けて扱う。
5. 製品対象に合うbuild・試験をCIへ追加する。実行SHA、artifactハッシュ、署名結果、Mac導入映像、Windows回帰を対応付ける。CI成功だけで物理Macのcaptureを成功としない。
6. `docs/macos/03_MEDIA_AND_PROTOCOLS.md` のAU上限対策・timerに関する旧記述を直す。`05_IMPLEMENTATION_PLAN.md` の段階0–1、`macos/README.md` の試験状態、`07_STATUS_AND_HANDOFF.md` の実施記録も更新する。実際の最新SHAを根拠とし、公開手順に承認、失敗表示、更新、削除を含める。

**完了条件**: 配布する正確なartifactが、対象OS・CPUの通常設定で導入され、一般アプリで映像をcaptureできる。署名、notarization、更新、削除、依存点検、ライセンス、同梱内容、手順書を同じ版とartifactハッシュへ結び付けて記録する。未実施のOS・CPU・音声経路は対応対象へ含めない。

### 依存点検（段階10 item4）

`npm ci --prefix cloud` 時点のauditでは15件の警告（2 low、5 moderate、6 high、2 critical）があった。

| パッケージ | 深刻度 | 種別 | 影響 |
|---|---|---|---|
| @vitest/mocker | moderate | devDep | Vitestモックリダイレクト経由のファイル読み取り。開発時のみ |
| cookie / youch | high | devDep | miniflare（ローカルワーカーランタイム）内のcookie解析。開発時のみ |
| devalue | high | devDep | プロトタイプ汚染・DoS。Svelte内包。開発時のみ |
| esbuild | moderate | devDep | 開発サーバーへの外部リクエスト。開発時のみ |
| undici | high | devDep | HTTP/1.1応答分割等。miniflare内。開発時のみ |
| ws | high | devDep | 未初期化メモリ開示・DoS。miniflare内。開発時のみ |

全件が devDependencies（vitest・wrangler・miniflare）に閉じており、プロダクション依存は影響を受けない。これらのツールはローカル開発時のみ動作し、同梱物には含まれない。

| 項目 | 内容 |
|---|---|
| 影響範囲 | ローカル開発環境のみ。プロダクションコード・同梱物への影響なし |
| 更新可否 | `audit fix --force` はメジャーアップデートを要求。互換性検証が必要 |
| 対処方針 | 開発ツールは最新のマイナー系に追従。プロダクション依存は変更なし。CIで audit を定期実行 |

### 段階10 item4 依存点検（2026-09-28）

| 項目 | 内容 |
|---|---|
| 目的・合格条件 | 依存の脆弱性・ライセンス・同梱版を点検する |
| 結果 | 15件（2 low, 5 moderate, 6 high, 2 critical） |
| 対象 | undici, ws（@cloudflare/vitest-pool-workers 依存） |
| 影響 | 開発依存のみ。製品コード（ブラウザ送信・host）は影響なし |
| 対処 | `npm audit fix --force` は破壊的変更のため、製品リリース前に要判断 |

## 残る判断の担当と時期

段階4の署名前に開発用識別子を設定する担当を決める。段階8の測定後に映像の許容fps・遅延・負荷と対応OSを決める。段階9の開始前に音声の製品範囲を決める。段階10の配布作業前に配布経路と配布用署名の管理方法を決める。決定した値と根拠は[設計判断](../../docs/macos/08_DECISIONS_AND_REFERENCES.md)または後続の決定記録へ反映する。
