# 12. リスク、判断記録、未確定事項

## ADR-001 Cloudflare Worker + Durable Object

**決定:** 採用。

**理由:**

- Worker API自体をstatelessに保ちつつ、Session単位の短期stateを強整合に管理できる。
- Free planでもPoC可能な範囲。
- AlarmでTTLを実装できる。
- D1のtable/conditional updateより単純。

**結果:** D1/KVは初期版に入れない。

## ADR-002 HTTPS polling

**決定:** 1〜2秒polling。

**理由:**

- Windows側listen不要。
- Browser/Windowsともoutbound HTTPSのみでsignaling可能。
- WebSocket lifecycleが不要。
- 初回接続だけなのでrequest数が限定的。

**リスク:** Free quota消費、接続開始が最大poll間隔分遅れる。

**対策:** 成功後即停止、60秒timeout、将来SSE/Trickleを別phase。

## ADR-003 Non-Trickle ICE

**決定:** 初期版で採用。

**理由:** Candidate API/state/revisionを省き、Offer/Answerだけにする。

**リスク:** 接続開始時間増加、応答しないcandidate URLによる待ち。

**対策:** gathering timeout、port 53 URL除外、将来Trickle。

## ADR-004 BrowserがOfferer

**決定:** 採用。

**理由:**

- getUserMedia後にsendonly trackを明示してOffer生成しやすい。
- WindowsはOfferをpollしてAnswer。
- QR flowと自然に一致。

## ADR-005 native upstream libwebrtc

**決定:** 採用。

**理由:**

- Browser相互運用性。
- ICE/DTLS-SRTP/RTP/audio/video codecをまとめて扱える。
- 将来iOS senderとも同じWebRTC semantics。

**リスク:**

- 安定SDKではない。
- buildが重い。
- Windows toolchain要件が変わる。
- API break。

**対策:** commit pin、GN args lock、build script、wrapper boundary。mainを直接追わない。

## ADR-006 Win32 C++20 UI

**決定:** 採用。

**理由:**

- libwebrtc、D3D11、Media Foundation、COMとの境界が単純。
- Qt/WinUI等の大型依存を避ける。
- PoC UIは小さい。

**リスク:** UI code量。

**対策:** UIは最小限、business logicをcontrollerへ分離。

## ADR-007 Virtual CameraはMedia Foundation

**決定:** 採用。

**理由:**

- Windows 11標準のuser-mode Virtual Camera API。
- kernel camera driver不要。
- Camera/会議アプリへ列挙可能。

**リスク:** Media SourceがFrameServerへloadされ、debug/IPC/installerが複雑。

**対策:** Microsoft sampleを基準、Media Source依存最小、独立test harness。

## ADR-008 v1 IPCは名前付きパイプ

**決定:** 採用。

**理由:**

- FrameServerとのprocess/session境界で扱いやすい。
- admin privilegeなしでReceiverがserverを作れる。
- connection lifecycleが明確。
- 低遅延は後工程。

**リスク:** 720p30で約41.5MB/sのcopy、CPU/latency。

**対策:** overlapped I/O、latest-only、1 client、将来shared memory/D3D texture。

## ADR-009 仮想カメラ形式固定

**決定:** 720p30 NV12のみ。

**理由:** negotiation、scaling、timestamp、consumer互換を最初に安定させる。

**リスク:** 1080pや異なるaspect ratioを利用できない。

**対策:** Receiverでletterbox。media type追加はPoC後。

## ADR-010 仮想マイクを作らない

**決定:** VB-CABLE等へrender。

**理由:** Windows virtual audio driver、signing、installerを初期scopeから除外。

**リスク:** 外部ソフト導入、device名/設定の混乱。

**対策:** generic endpoint selector、UI guide、同梱しない。

## ADR-011 H.264 onlyにしない

**決定:** VP8 fallbackを維持。

**理由:** Browser/libwebrtc build間でH.264 availabilityが変わり得る。PoC接続性を優先。

**将来:** iOS senderではH.264 hardware encodeを優先し、Windows hardware decodeを検討。

## 主要リスク一覧

| Risk | 影響 | 確率 | 対策 |
|---|---|---:|---|
| libwebrtc build不能 | 高 | 中 | compatible revision pin、toolchain確認 |
| H.264 support不足 | 中 | 中 | VP8 first、build flags検証 |
| Virtual Camera DLL load失敗 | 高 | 中 | dependency最小、dumpbin、official sample |
| FrameServer pipe ACL不一致 | 高 | 中 | identity確認、explicit ACL、diagnostic |
| Pipe throughput不足 | 中 | 低〜中 | latest-only、測定、shared memory移行 |
| VB-CABLE device selection不安定 | 中 | 中 | endpoint ID、WASAPI fallback |
| STUN onlyで接続不能 | 高 | 高（環境依存） | TURN short credential |
| Polling quota増加 | 中 | 低（PoC） | timeout、stop、usage monitor |
| QR先取りclaim | 中 | 低 | 5分/one-time、将来SAS |
| Browser background制限 | 中 | 中 | active UI、manual test |
| Windows privacy consent | 中 | 中 | background create、UI guidance |
| Media Source crash affects camera | 高 | 低〜中 | input validation、test harness |
| Token leak in logs | 高 | 中 | central redaction、tests |

## 実装時に確定すべき項目

コーディングエージェントは以下を調査・固定し、`IMPLEMENTATION_STATUS.md`とlock fileへ記録する。

1. libwebrtc commit SHA
2. depot_tools revision
3. Visual Studio version
4. Windows SDK version
5. GN args
6. H.264 build/interop結果
7. AudioDeviceModuleでのendpoint選択可否
8. Virtual Camera Media Source project方式
9. FrameServer process identityとpipe ACL
10. exact COM registration keys
11. TURNを有効にするproduction policy
12. static asset build tool version
13. Cloudflare compatibility date
14. installer technology
15. third-party licenses

## Scope change rule

次をCore PoCへ無断追加しない。

- account/login
- database
- WebSocket
- SFU
- recording
- screen sharing
- multi-session
- automatic reconnect with ICE restart
- Electron/Qt/WinUI migration
- custom audio driver
- 1080p/4K
- latency optimization

必要なら別ADRとmilestoneを作り、既存acceptanceを先に満たす。
