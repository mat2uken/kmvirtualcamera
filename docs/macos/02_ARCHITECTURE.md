# 02 アーキテクチャと共通化の境界

## 完成形の設計

```text
Browser (existing RTP H.264 / WebCodecs DataChannel)
            |           Cloudflare signaling remains unchanged
Shared receiver C++20: session / signaling model / RTC / compressed media
            |
            +-- Windows video backend: MF + D3D11/CPU + existing publisher
            |       -> existing virtual camera Media Source DLL
            |
            +-- macOS video backend: VideoToolbox -> CVPixelBuffer / IOSurface
                    -> normalize -> application-side CoreMediaIO sink publisher
                    -> Camera Extension sink -> latest decoded frame -> source
                    -> camera consumer applications
```

共有するのはネットワーク、セッション、圧縮AU、時刻変換、変換の意味、状態・統計です。
OSのGPUバッファ、デコードAPI、デバイス登録、UI、IPCは別実装です。
共通化のためにデコード結果を必ずCPUの `vector<uint8_t>` へ変換する設計にはしません。

## 現在の実装との差

現在の `km_media_core` はDataChannel再構成と小さな共通ヘッダーの基盤です。
`km_rtc_shared` は既存RTP／RTCPの抽出先で、オプションビルドです。
`ReceiverEngine`（`shared/receiver/engine`）とシグナリングワーカー（`shared/receiver/signaling/signaling_worker`）は共通C++で単体試験済み、AppKit hostへワーカー配線とQR join UIも実装済みですが、実RTC接続（Answer生成）は未実装で、`PeerConnectionManager` からの切替と `AppController` との分離は次の作業です。
macOSのデコーダ・relay・有効化managerも、それぞれを接続する実アプリをまだ持ちません。

## 3つの基本境界

`shared/km/receiver_contracts.h` は次の境界を宣言します。

| 境界 | 契約 | OS別 |
|---|---|---|
| `IHttpTransport` | method、URL、headers、body、timeout、応答上限、cancel | WinHTTP / NSURLSession |
| `IVideoPipeline` | owning EncodedVideoFrame、start、submit、transform、stop、backpressure | MF / VideoToolbox |
| UIイベント境界（次段階） | 状態値とユーザー操作。ネイティブウィンドウ型を含めない | Win32 / AppKit |

HTTPの現契約はsignaling workerでの同期呼び出しです。UI・RTCコールバック・video workerをブロックしません。
NSURLSessionアダプターは内部で非同期通信を使い、キャンセル可能なworker側待機へ接続できます。
安易な無期限semaphore待機を導入しないでください。

映像パイプラインのsubmitはバッファの所有権を受け取り、保持範囲を明示します。
start/stop/config変更は単一owner executorへ直列化し、callbackにもgenerationを付与します。
`macos/receiver/video_pipeline.mm` が `IVideoPipeline` の実装で、owning Annex-B AUを保有workerが直列デコードします。
queue満杯は依存列を破棄してBackpressure、デコード失敗は次のrandomAccess要求に戻ります。
VideoToolboxの補助クラス（`native_media.h` のデコード・正規化）はこの実装の内部部品です。

## プロセスとライフサイクル

受信アプリ: UI、QR、シグナリング、ICE/DTLS、デパケタイズ、デコード、正規化、プレビュー、Extension有効化。
Extension: device/stream公開、producer検証、最新フレーム、出力ペーシング、待機画面だけ。
カメラ登録状態、ネットワーク接続状態、映像供給状態、利用アプリのcapture状態を別々に管理します。
プレビュー非表示やウィンドウ最小化でエンジンを止めません。明示的stop／アプリ終了時には映像を破棄します。

Extensionのsource開始は参照カウントで管理し、複数consumerの一方が止めても他方の出力を止めません。
producerは当初1つだけ許可し、producer終了／ユーザー切替／再接続時は旧映像・旧callbackを失効させます。

## 依存とビルド

| ターゲット | 依存 |
|---|---|
| `km_media_core` | C++標準ライブラリのみ |
| `km_rtc_shared`（任意） | 上記＋インストール済みlibdatachannel |
| Windows Receiver | 既存libdatachannel / mbedTLS とOS API |
| `km_macos_buffers` | Foundation / CoreVideo / CoreMedia |
| `km_macos_decoder` | buffers + VideoToolbox |
| `km_camera_relay` | buffers + CoreMediaIO。decoder/RTC/TLSをリンクしない |
| `km_extension_manager` | Foundation / SystemExtensions |

libdatachannelの推移的依存（ICE、SCTP、SRTP等）も配布物に含めて管理します。
既存のpinはmbedTLS3.6.2、libdatachannel0.22.4です。これは調査時のコード値で、現在の推奨最新版という意味ではありません。
移植と無関係な大型upgradeを同時に行わず、配布前に別途セキュリティ・対応版を確認します。
WebSocket不要設定を維持しても、RTP MediaTrack対応を残すためmedia機能を無効化してはいけません。

新しい汎用UIフレームワーク、libwebrtc、FFmpeg、libcurlは追加しません。
HTTPはOS標準、QRは既存小規模コードを再利用する方針です。
既存のC++をRustまたはSwiftへ全面書き換えしません。

## 移動方法

Windows既存CMake/scriptsとの互換のため、移動元の `.h/.cpp` はshared側をincludeする薄い転送ファイルです。
過渡的な方法であり、最終的にはWindowsターゲットも共通ライブラリへリンクします。
同じ実装の転送 `.cpp` と共通ライブラリを同時にリンクしないことが必須です。
現在のroot CMakeと `windows/CMakeLists.txt` は別のビルド入口です。

OS下限はCamera Extension APIの12.3を基準にしていますが、製品サポート範囲は実機マトリクスが決めます。
最初にApple Siliconで成立を確認し、Intel版を提供する場合は全依存を含めx86_64を追加検証します。
