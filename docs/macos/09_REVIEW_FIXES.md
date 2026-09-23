# 09 未修正監査項目 R01–R13 の追加修正

作成日: 2026-09-23。適用基準: `f7c7eb3777a00924e8498fb2ca1d0d277b1b5951`。
対象: `feature/macos-coremediaio-foundation`。この資料は追加修正パッケージの内容です。
**コードへの対策実装と、Windows/macOSでの製品動作確認は別です。後者は未実施です。**

## 対応表

| ID | 追加した対策 | 検証と残る確認 |
|---|---|---|
| R01 | RTP共通header/CSRC/extension/padding検証。STAP-Aの長さ0、途中切れ、不正NALを変更前に拒否。FU-Aの開始/終了/reserved/typeを検証 | portable回帰試験、ASan/UBSan。不正RTP/RTCP 50,000件の固定seed入力 |
| R02 | 古い/重複RTPはpayload処理前にreturn。AU/FU合計4MiB、parameter set各64KiB。欠損後は完全なSPS/PPS付きIDRまで停止。SSRC変更でcacheを破棄 | loss、duplicate、wrap、SSRC、未完FUからの切替を試験。RTPの並べ替え待機は実装せず、低遅延のため遅着を破棄 |
| R03 | compound RTCP全体のversion/length/padding/report countを検証後、SR/RRをbyte単位で読取。RR送信者SSRCを映像SSRCに代入しない。wide算術でjitterを計算 | wire validatorはsanitizer試験。libdatachannelに接続したhandler全体はネイティブビルド/相互運用試験が必要 |
| R04 | Opus RTPを実際に復号。callbackのsize_tはinterleaved int16要素数。rendererへのspanでchannelsを再乗算しない。WASAPI専用thread、固定48k stereo PCM、bounded ringを追加 | インストール済み実libopusで無音パケットの復号、1920要素、PLC、reorder、wrapを試験。WASAPI出力/実マイク/リップシンクは未検証 |
| R05 | DecoderをVideoWorkerProcのローカル変数にし、そのthreadでCOM初期化、初期化/再設定/復号/破棄。再設定時も同じD3D deviceを渡す | コードレビューのみ。Windows SDKビルド、カメラ切替/再接続試験が必要 |
| R06 | RTC層でRTP32bit90kHzをunwrap後microsecondsに変換。DCは32bitmicrosecondsをunwrap。session/SSRC変更でreset。送信時刻と出力host時刻を区別 | 既存timing helperを呼出し箇所へ接続。実ネットワークでの長時間wrap試験が必要 |
| R07 | liveとidleのPublisherを単一30fps workerに統合。steady clock epochと有理数pacerを使用、遅延時は過去のslotをskipしcatch-up burstを避ける | 接続箇所を実装。OS scheduler/仮想カメラでのpacingとdriftは未測定 |
| R08 | SPSC前提を廃止したmutex保護の8AU/2MiB queue。満杯時は依存列を破棄しrecovery serialを更新、PLIを要求。古いdecode結果もserialで拒否 | overflow、configured IDR、世代更新、古い結果の拒否を回帰試験。旧クラス名は互換wrapper |
| R09 | JSON内のICE urls/user/credential/policyを完全に引き継ぐ。URLのhost/IPv6/port/transportはrtc::IceServerへ委譲。勝手なSTUN追加を廃止 | JSON保存とIPv6文字列試験。実TURN relay試験は未実施。libjuiceのTCP/TLS制限は下記 |
| R10 | bounded JSON grammarとschemaを共通化。空白/escape/Unicode/サロゲート/重複key/型/深さ/末尾を検証。WinHTTP実行をIHttpTransportに分離。1MiB応答上限、固定読取buffer、TLS検証、redirect禁止、cancel/deadline | JSONとmock HTTP回帰試験。WinHTTP transportはWindows SDKとネットワーク試験が必要 |
| R11 | v1の1180×255=300900 byte上限をAU全体で事前検証。送信途中でcountをuint8へwrapさせない。送信失敗/oversize後はIDR待ち。oversizeではbitrate低下、3回連続なら停止し解像度変更を案内 | helper/実senderの模擬出力、255境界、oversize時send0回、途中send例外、停止/track世代をTypeScript+Nodeで試験 |
| R12 | RTC専用20ms timerを追加。無通信時にもDC timeout/PLI、音声jitter、RTCP feedbackを進める。Closeで停止/join | 配線実装。無通信/Closeと競合する実libdatachannel試験は未実施 |
| R13 | 全libdatachannelアプリcallbackにweak gateとactive lease。Closeは新規callbackを閉じ、実行中完了とtimer終了を待ってから状態を破棄。signaling serialとUI世代も確認 | gateの実行中待機/新規拒否をthread回帰試験。Close/Initialize/破棄はcontrol thread限定。フルアプリのrace試験は別途 |

## 関連する追加修正

- Windows UI更新をmessage-only windowへのPostMessage経由へ移動。workerのSetWindowTextによる同期SendMessageとmain thread joinの循環待ちを回避。
- 音声を10msのWASAPI空き領域へ直接切り詰めず、200msを上限とするPCM ringでworkerへ渡す。
- application PCM形式を48kHz/stereo/16bitに固定し、OS変換flagを利用。不明なmix formatにstereoを書き込む旧fallbackを廃止。
- ブラウザのping timerをstop時に破棄。track/encoder世代で古いframe callbackを拒否し、VideoFrameを必ずclose。
- 初期設定や接続失敗を、存在しない成功として表示しない。音声無効構成では音声trackを拒否。

## 新規依存とビルド

音声復号用にlibopus 1.6.1を追加しました。CMakeは公式tarballとSHA256を固定します。
`OPUS_DRED=OFF`、`OPUS_OSCE=OFF`、programs/tests/shared libraryを無効にし、音声無効構成も残します。
Root CMakeの`KM_ENABLE_OPUS`はOFF、Windows production入口ではONが既定です。
仮想カメラDLL/Camera ExtensionにはOpusやWebRTCをリンクしません。

既存Windowsターゲット定義は内容を変更せず`windows/legacy_targets.cmake`へ移動し、
`windows/CMakeLists.txt`を依存と追加sourceの接続点にしています。
旧テストで不足していたDC translation unitも接続します。

## TURN TCP/TLSはビルド依存

libdatachannel v0.22.4の既定libjuice backendは、クライアントからTURNへのUDP接続が対象です。
TCP/TLSのTURN URLを渡しても対応したと説明してはいけません。
既定構成はTCP/TLSを理由付きでskipし、relay-onlyに使用可能なUDP TURNがなければ初期化失敗にします。
TCP/TLSが必要な場合はlibniceを用意して、Windows入口に`-DUSE_NICE=ON`を指定します。
Rootのインストール済みlibdatachannelを使う試験では、実際にlibniceでbuildしたものに限り
`-DKM_INSTALLED_RTC_USES_LIBNICE=ON`とします。flagだけ変更して能力を偽装してはいけません。

## 残る制約

- パケットごとのtimestampをmicrosecondsに揃えても、送信端末と受信PCの時計が同期するわけではありません。
- 音声は最大20msの初期jitter windowと40msのgap待機を持ちます。動画との適切な遅延合わせは別の実測工程です。
- Closeをleased callback内で呼ぶことは禁止し、検出時はlogic_errorにします。callbackはcontrol/UI threadへ操作をpostしてください。
- UIKit/AppKit/Win32のウィンドウ寿命、OS callback、device lost、スリープ復帰を実機で検証してください。
- 既存macOS部品は今回更新していません。署名済みホスト、Provider/Device/Streams、host-side sink publisherなどは引き続き実装が必要です。
- 単体試験の成功は脆弱性ゼロ、RFC完全準拠、リップシンク保証、完全ゼロコピーを意味しません。

## 一次資料

- https://www.rfc-editor.org/rfc/rfc3550 （RTP/RTCP）
- https://www.rfc-editor.org/rfc/rfc6184 （H.264 RTP payload）
- https://www.opus-codec.org/docs/opus_api-1.6/group__opus__decoder.html
- https://www.opus-codec.org/release/stable/2026/01/14/libopus-1_6_1.html
- https://github.com/paullouisageneau/libdatachannel/blob/v0.22.4/DOC.md
- https://github.com/paullouisageneau/libdatachannel/blob/v0.22.4/include/rtc/configuration.hpp
- https://learn.microsoft.com/en-us/windows/win32/winhttp/option-flags
