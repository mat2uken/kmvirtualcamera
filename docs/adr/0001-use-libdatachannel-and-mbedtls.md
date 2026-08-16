# ADR 0001: libwebrtc から libdatachannel + mbedTLS への移行と最小依存方針

## ステータス
承認済 (Accepted)

## 背景 (Context)
当初の仕様では、Windows受信側でGoogle upstreamの `libwebrtc`（Chromium WebRTCスタック）を採用する方針でした。
しかし、`libwebrtc` には以下の重大な課題があります：
1. **巨大な依存とツールチェーン**: `depot_tools`、数GB〜数十GBのソースコード、Chromium専用ビルドツール（GN/Ninja）、特定のClang/MSVCバージョンへの強固な束縛。
2. **保守性とビルド再現性の難しさ**: 依存関係が極めて大きく、独立したクリーンなC++プロジェクトとしてのビルド・保守が極めて重い。
3. **プロジェクト目標との乖離**: 本プロジェクトは最小限のフットプリントと高い保守性・依存最小化を最重要要件としている。

## 決定事項 (Decision)
1. **WebRTCスタックの変更**:
   - `libwebrtc` を完全に廃止し、**`libdatachannel`** を採用する。
   - `libdatachannel` はC++17/20で書かれた軽量・高機能なWebRTCライブラリであり、PeerConnection、ICE (libjuice)、DTLS、SRTP、RTP Media Trackを完全サポートする。
2. **TLSライブラリの選定**:
   - 最も軽量な **`mbedTLS`** をTLS/暗号バックエンドとして使用する (`USE_MBEDTLS=ON`)。
3. **プロジェクト全体の依存極限最小化 (Zero/Minimal External Dependency)**:
   - **シグナリングクライアント**: 外部ライブラリを使わず、Windows標準の `WinHTTP` を直接使用。
   - **UI / 描画**: 外部UIフレームワークを使わず、Win32 API + Direct3D 11 (`d3d11.dll`) を直接使用。
   - **仮想カメラ**: Windows 11 標準 Media Foundation (`mfplat.lib`, `mfvirtualcamera.h`) のみを使用。
   - **IPC**: Windows 標準 Overlapped Named Pipe を使用。
   - **QRコード生成**: 単一ヘッダー/ソースの極小 Nayuki QR Code Generator を使用。
   - **Cloudflare Worker / 送信側Web**: 外部npmフレームワーク・ライブラリを排除し、標準Vanilla TypeScript + Web Crypto + Workers SQLite Durable Objectsで実装。
   - **メディアコーデック/デパケタイズ**:
     - 映像: RTP H.264/VP8パケットをデパケタイズし、Windows標準のMedia Foundation MFT (Media Foundation Transform) デコーダーまたは極小デコーダーでデコード。
     - 音声: `libopus` (単体軽量Cライブラリ) でデコードし、Windows WASAPI (`mmdevapi.dll`) で `CABLE Input` へ直接出力。

## 影響 (Consequences)
- **メリット**:
  - `depot_tools` や数十GBのChromiumビルド環境が不要になり、標準的なCMake + MSVC 2022だけで一貫して高速ビルド可能になる。
  - プロジェクト全体のサイズと依存が極限まで削減され、セキュリティ攻撃面が最小化される。
  - ランタイムバイナリサイズが大幅に削減される。
- **留意事項**:
  - メディアトラック（RTPパケット）の受け取りからデコード（H.264/VP8/Opus）およびWASAPIへの出力処理を、シンプルかつ堅牢に実装・テストする。
