# 01 コード監査と修正状況

基準main: `1f22e7423748a1de6469e40ef5516a29d9b19fa8`。
初回macOS土台: `f7c7eb3777a00924e8498fb2ca1d0d277b1b5951`。
2026-09-23追加修正: **R01–R13に対策コードを実装。製品動作確認は未実施。**
現在の詳細と検証範囲は [09_REVIEW_FIXES.md](09_REVIEW_FIXES.md) を参照してください。
旧文書の「R01–R13は未修正」という記述は、初回土台コミット時点の履歴です。

## READMEでは分からなかった実経路

- ブラウザには通常のMediaTrackとWebCodecs＋unreliable DataChannelの2経路がある。
- Windowsの主経路はCPU版DecodeAccessUnit → NV12変換 → Publisher → preview。
- GPU用APIが存在することと、主経路でCPUコピーがないことは異なる。
- PublisherはDXGI共有texture、共有memory、Named Pipeの更新を持つ。
- macOSではこれら3種類のIPCを複製せず、CoreMediaIO標準sinkを優先する。

## 初回土台で修正した項目 A01–A07

| ID | 元の問題 | 修正 |
|---|---|---|
| A01 | DC映像AUを無条件でH264/JSONLへ保存・flush | デパケタイザの自動保存を廃止 |
| A02 | 同一seq内のtotalChunks変更を検証せず配列へアクセス | count/timestamp/flag整合を先に検証 |
| A03 | 初回IDR待ちで期限掃除前にreturn | 初期化前も掃除、32frames/2MiB上限 |
| A04 | packed structをwireへ直接cast、payload上限なし | byte単位LE読取、length/flag/index検証 |
| A05 | C++はpayload1168、ブラウザは1180 | 実送信と一致する1180/packet1192へ統一 |
| A06 | mutex保持中の外部callback | lock外通知、reentrant Reset試験 |
| A07 | SPS/PPSとkey flagの簡易判定 | IDR NAL確認、parameter set対の更新検証 |

## 今回追加した項目

| ID | 問題 | 対応先 |
|---|---|---|
| R01/R02 | STAP-A/FU-A境界、古いRTP混入、AU蓄積上限 | `shared/km/rtp_wire.h`, `shared/receiver/codec/h264_rtp_depacketizer.*` |
| R03 | SR/RRへの不十分な長さ確認後のcast | `shared/receiver/rtc/enhanced_rtcp_session.*` |
| R04 | 圧縮OpusをPCM扱い、channels二重乗算 | `shared/receiver/audio/opus_rtp_decoder.*`, WASAPI, AppController |
| R05 | decoderの別thread再初期化、D3D device脱落 | AppControllerのVideoWorkerProc |
| R06/R07 | 90kHz/us混在、count×16666と33ms sleep | PeerConnectionManagerのunwrap、単一出力workerとRationalPacer |
| R08 | 16MiB予約SPSC、Push失敗無視 | `shared/km/bounded_video_queue.h`とPLI/recovery serial |
| R09/R10 | ICE設定廃棄、部分JSON、無制限HTTP応答 | `shared/receiver/signaling/session_*`, WinHTTP transport |
| R11 | browserの255分割超をuint8へ書込 | `cloud/web/src/dc_packetizer.ts`, `webcodecs_sender.ts` |
| R12/R13 | timer未接続、raw-this寿命/旧世代競合 | PeerConnectionManagerのtimer/gate/serial、AppControllerのUI post |

これらは「対策実装済み・部分検証済み」です。RTP/RTCP handler全体、WinHTTP、WASAPI、
COM仮想カメラ、WebRTC実接続は、それぞれのOSと本物の依存でビルド/動作確認が必要です。
[検証記録](07_STATUS_AND_HANDOFF.md) と [詳細表](09_REVIEW_FIXES.md) を併読してください。

## 履歴の確認

```sh
git show 1f22e7423748a1de6469e40ef5516a29d9b19fa8:windows/receiver/codec/dc_video_depacketizer.cpp
git show f7c7eb3777a00924e8498fb2ca1d0d277b1b5951:docs/macos/01_CODE_AUDIT.md
```
