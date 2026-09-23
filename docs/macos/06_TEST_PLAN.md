# 06 更新版試験計画

## 共通試験

```sh
sh scripts/test_foundation.sh
KM_BUILD_DIR="$PWD/build/foundation-asan" CC=clang CXX=clang++ \
  sh scripts/test_foundation.sh -DKM_ENABLE_SANITIZERS=ON
KM_BUILD_DIR="$PWD/build/foundation-opus" \
  sh scripts/test_foundation.sh -DKM_ENABLE_OPUS=ON
```

root CMakeのデフォルトはOpus無効であり、ネットワーク取得なしの共通試験を残しています。
Opus有効時は公式tarballの取得が必要です。オフラインでは検証済みのソースを
`-DFETCHCONTENT_SOURCE_DIR_KM_OPUS=/path/to/opus-1.6.1`で指定します。

## ブラウザ

```sh
npm ci --prefix cloud
sh scripts/test_browser_protocol.sh
npm run --prefix cloud build
npm test --prefix cloud
```

追加fixtureは模擬DataChannel/Encoderであり、Safari/Chromeの実機試験ではありません。
255chunks、300900byte、oversize時送信0、途中例外後のIDR待ち、track切替と停止後の旧出力を確認します。
実ブラウザでは前面/背面切替、portrait/landscape、ネットワーク遮断、解像度低下を試験します。

## Windows

```powershell
cmake -S windows -B build/windows -DKM_ENABLE_OPUS=ON
cmake --build build/windows --config Release --target Receiver test_h264_depacketizer test_receiver_startup test_webrtc_dtls
ctest --test-dir build/windows -C Release --output-on-failure -R H264DepacketizerTest
```

CTest全件は仮想カメラ登録/captureを行う試験を含むため、勝手に全件実行せず既存の手動手順を確認します。
音声はPCM要素数1920/20msのstereo、device変更、underrun/overflow、48k以外のendpointを確認。
Closeとpacket/timer/UI callbackを競合させ、use-after-free、deadlock、旧世代の表示がないことを確認。

## TURN

実際にUDP TURNを用意してrelay-onlyで接続し、candidate pairがrelayであることを確認します。
既定libjuice構成でTCP/TLS-only設定を渡すと理由付きの失敗になることを試験します。
libniceへ切替えた構成だけでTCP/TLSを検証し、flagのみで対応を主張しません。

## macOS

```sh
sh scripts/test_macos_foundation.sh
```

このスクリプトは署名/Extension導入を行いません。Mac componentsがcompileできた後で、
生成映像source → host sink → browser接続を別々に試験してください。
3consumer、停止/再開、更新/無効化、スリープ、ユーザー切替、黒画面へのtimeoutを含めます。

## 記録

現時点の実行記録は [07_STATUS_AND_HANDOFF.md](07_STATUS_AND_HANDOFF.md)。
GitHub workflowは試験を自動化する定義であり、ファイルが存在するだけで通過済みとは扱いません。
