# 06 テスト・受入計画

## 実施済みの範囲

この基盤作成時、Linux上で共通C++の2テストをGCC ReleaseとClang ASan/UBSanで実行しました。
macOS SDK、Windows SDK、実カメラ利用アプリを使う試験は未実施です。詳細は [07](07_STATUS_AND_HANDOFF.md)。

## 共通テスト

```sh
sh scripts/test_foundation.sh
cmake -S . -B build/foundation-asan -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_BUILD_TYPE=Debug -DKM_ENABLE_SANITIZERS=ON
cmake --build build/foundation-asan --parallel
ctest --test-dir build/foundation-asan --output-on-failure
```

現在の `common_contracts` は時刻unwrap、単位変換、overflow、30fpsの108000区間、30000/1001、NAL境界、generation、NV12 stride/padding/回転を扱います。
`datachannel_regression` は順不同、重複、sequence wrap、metadata不一致、初回IDR待ち期限、保持上限、損失後IDR、偽key flag、1180payload、AVCC、callback内Resetを扱います。
固定seedの不正パケット20000件を含みますが、網羅的fuzzやネットワーク負荷試験ではありません。
期待結果をreleaseビルドでも評価するため、C assertのNDEBUGに依存しないCHECKを使用します。

追加必須: RTPの空STAP-A/zero NAL/欠損FU/異常padding/拡張長/重複、RTCP短いSR/RRとcompound境界、ブラウザ255chunk超。
thread-sanitizer試験と実際のstop/reconnect並行実行は、現在のASan/UBSan結果では代替できません。

## Windows

```powershell
cmake -S windows -B build/windows
cmake --build build/windows --config Release --parallel
ctest --test-dir build/windows -C Release --output-on-failure
```

既存 `scripts/test_pipeline_e2e.ps1` や登録・仮想カメラcapture試験は、内容を確認した上で専用環境で実行します。
それらにはOS登録や実ネットワークを必要とするものがあるため、共通単体試験と同じ無副作用の試験ではありません。
転送cppを使った直接コンパイル試験は成功していますが、MSVC/COM/MF/D3D11/WASAPIの試験成功を意味しません。
RTP/DCの既存映像、UI再接続、登録／停止、回転、カメラ切替、CPU／GPUフォールバックを比較します。

## Macの段階別ゲート

| ゲート | 試験 | 成功の証拠 |
|---|---|---|
| Native | `sh scripts/test_macos_foundation.sh` | SDK buildログとCLI/CTest結果 |
| Decoder | 有効な単一AU、parameter変更、decode error | dimensions/format/hardware状態、retain leakなし |
| Extension | signed host同梱、activation、source生成映像 | 列挙結果、capture frame、時刻進行 |
| Sink | host生成フレーム→sink→source | generation/counterの一致、遅延、所有権 |
| Browser | Safari/Chrome、RTP/DC両方式 | スマホ映像を実利用側で取得 |
| Distribution | signed/notarized配布物 | クリーンMacの通常設定でinstall/update/remove |

## E2Eマトリクス

入力はiOS SafariとAndroid Chrome、受信は少なくともApple SiliconとWindows既存環境を用意します。
Intel Macを対応対象へ含める場合、別途x86_64 build＋実機を必須にします。
カメラ利用側は少なくともOS標準の取得経路と実際に利用予定の会議／配信アプリで試験し、アプリ名・版を記録します。

| 項目 | 条件と観測 |
|---|---|
| 表示 | 720p30、portrait/landscape、0/90/180/270、aspect fit、SPS解像度変更 |
| 色 | 601/709、video/full-range、Y/UV stride差、黒の値、clean aperture、SAR |
| 接続 | 初回QR、timeout、Offer失敗、ICE再接続、TURN強制、IPv6、オフライン |
| 制御 | torch/zoom/camera switchのcapabilityとunsupported表示 |
| 利用側 | 0/1/2consumer、片方停止、producer2重起動、不正producer |
| 寿命 | receiver終了、extension再起動、sleep/wake、ユーザー切替、callback遅延 |
| 障害 | packet loss/reorder、IDR未着、過大AU、queue満杯、decode失敗、host異常終了 |
| 長時間 | 80分以上または注入時計でDC32bit wrapを越える。RSS・queue・PTS・復旧を観測 |
| プライバシー | 既定で映像dumpなし、終了後黒画面、tokenをログへ書かない |

80分の実測待ちができないCIでは注入時刻によるwrap単体試験を行い、別枠でsoakを残します。
出力タイムスタンプ単調性、frame間隔分布、decode時刻、sink滞留時間、RSSとallocation数をログ化します。
遅延を一律の数値で保証する前に、基準条件とp50/p95/p99、queue深さ、drop/recovery回数を測定します。
映像payloadを保存せずに得られる統計を優先し、映像fixture保存には明示opt-inと削除方針を設けます。
