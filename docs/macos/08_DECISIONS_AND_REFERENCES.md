# 08 決定事項・代替案・一次資料

## 設計判断

| ID | 採用 | 採らないもの／見直し条件 |
|---|---|---|
| D01 | C++20共通コア＋Objective-C++薄いOS層 | Swift/Rustへの全面書換え。新しい言語層が明確に保守量を減らす場合だけ再評価 |
| D02 | Camera Extension | 新規DALプラグイン、kernel driver。旧OS対応を製品要件として追加する場合は別途検討 |
| D03 | CoreMediaIO標準sink→source | 独自XPC/共有メモリは標準経路の制約が実測で判明した場合だけ |
| D04 | デコードは受信アプリ、Extensionは小さく | ExtensionへWebRTC、TLS、VideoToolboxを一括リンクしない |
| D05 | CVPixelBuffer/IOSurfaceをOS内に維持 | CPU配列への強制共通化。CPU処理は基準／フォールバックとして残す |
| D06 | AppKit・OS HTTP・既存QR | 共通UI frameworkやHTTP用の新しい大型依存を追加しない |
| D07 | 固定720p30／420vを初回目標 | 初回から多解像度・多fps・10bit/HDR・複数cameraを拡大しない |
| D08 | audioは独立した次段階 | Camera Extensionを作れば仮想マイクも完成するという扱いにしない |
| D09 | 生成映像→sink→RTCの段階試験 | 最初からネットワークと署名問題をまとめてデバッグしない |
| D10 | Windows転送cppを一時維持 | 最終的にはtarget_link_librariesへ一本化。転送実装との二重リンクは禁止 |
| D11 | libdatachannelをWindows pinと同一でFetchContent導入（`KM_FETCH_DATACHANNEL`、mbedTLS v3.6.2＋v0.22.4・mbedTLSバックエンド・static） | 公式brew formulaは無い（2026-09実測）ので、pinの保証できない入手経路（システム実装など）は採らない。OpenSSL/gnutlsバックエンドや別ピンへの同時差し替えは行わない。TURN TCP/TLSはlibjuice既定に無いため主張しない（`KM_TURN_TCP_TLS=0`でスキップ、relay-onlyは理由付き失敗）。配布物への組み込みはライセンス（mbedTLS: Apache-2.0/GPL-2.0選択、libdatachannel・libjuice: MPL-2.0、usrsctp・libsrtp: BSD、plog・json: MIT）と段階10で合わせて再判断 |

これらは今回の要件に対する設計判断です。コード量・配布サイズ・性能の実測比較が済んだという意味ではありません。
依存最小化はライブラリ数だけでなく、OS層、独自protocol、ビルド保守、推移的依存、署名・配布の複雑さを含めて判断します。

## Apple一次資料

実SDKのヘッダーを最終的なシグネチャ・availabilityの根拠とし、Web記事の断片だけで未確認API名を追加しないでください。

- [Creating a camera extension with Core Media I/O](https://developer.apple.com/documentation/coremediaio/creating-a-camera-extension-with-core-media-i-o)
- [WWDC22: Create camera extensions with Core Media IO](https://developer.apple.com/videos/play/wwdc2022/10022/)
- [CMIOExtensionStream](https://developer.apple.com/documentation/coremediaio/cmioextensionstream)
- [CMIOExtensionClient](https://developer.apple.com/documentation/coremediaio/cmioextensionclient)
- [CMIOExtensionScheduledOutput](https://developer.apple.com/documentation/coremediaio/cmioextensionscheduledoutput)
- [CoreMediaIO](https://developer.apple.com/documentation/coremediaio)
- [CMSimpleQueue](https://developer.apple.com/documentation/coremedia/cmsimplequeue)
- [VTDecompressionSession](https://developer.apple.com/documentation/videotoolbox/vtdecompressionsession)
- [VideoToolbox](https://developer.apple.com/documentation/videotoolbox)
- [CoreVideo](https://developer.apple.com/documentation/corevideo)
- [OSSystemExtensionRequest](https://developer.apple.com/documentation/systemextensions/ossystemextensionrequest)
- [SystemExtensions](https://developer.apple.com/documentation/systemextensions)
- [URLSession](https://developer.apple.com/documentation/foundation/urlsession)
- [Core Audio](https://developer.apple.com/documentation/coreaudio)

重要な確認対象selector/API:
`consumeSampleBufferFromClient:completionHandler:`、`sendSampleBuffer:discontinuity:hostTimeInNanoseconds:`、
`notifyScheduledOutputChanged:`、`CMIOStreamCopyBufferQueue`、`CMVideoFormatDescriptionCreateFromH264ParameterSets`、
`VTDecompressionSessionDecodeFrame`、`VTDecompressionSessionWaitForAsynchronousFrames`。

Web仕様に実装があることと、署名された実機アプリで組み合わせて動作することは別の検証です。

## コードとprotocolの一次資料

- [調査基準のリポジトリ](https://github.com/mat2uken/kmvirtualcamera/tree/1f22e7423748a1de6469e40ef5516a29d9b19fa8)
- [元のDCデパケタイザ](https://github.com/mat2uken/kmvirtualcamera/blob/1f22e7423748a1de6469e40ef5516a29d9b19fa8/windows/receiver/codec/dc_video_depacketizer.cpp)
- [実際のWebCodecs送信](https://github.com/mat2uken/kmvirtualcamera/blob/1f22e7423748a1de6469e40ef5516a29d9b19fa8/cloud/web/src/webcodecs_sender.ts)
- [受信controller](https://github.com/mat2uken/kmvirtualcamera/blob/1f22e7423748a1de6469e40ef5516a29d9b19fa8/windows/receiver/app/app_controller.cpp)
- [PeerConnectionManager](https://github.com/mat2uken/kmvirtualcamera/blob/1f22e7423748a1de6469e40ef5516a29d9b19fa8/windows/receiver/rtc/peer_connection_manager.cpp)
- [libdatachannel](https://github.com/paullouisageneau/libdatachannel)
- [RFC 6184: H.264 RTP payload](https://www.rfc-editor.org/rfc/rfc6184)
- [RFC 3550: RTP/RTCP](https://www.rfc-editor.org/rfc/rfc3550)
- [W3C WebCodecs](https://www.w3.org/TR/webcodecs/)

既存 `windows/CMakeLists.txt`、`THIRD_PARTY_NOTICES.md`、`specs/frame_pipe_protocol.md` も確認します。
ただし古い仕様と実コードの不一致は監査文書に記録し、無条件に仕様へ合わせて互換性を壊さないでください。
