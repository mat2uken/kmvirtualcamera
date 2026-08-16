# 11. 実装・納品計画

## 1. 原則

- 一度に全領域を実装しない。
- 各milestoneで実行可能な縦sliceを作る。
- 低遅延最適化は受信・仮想化が成立した後。
- 依存の固定と再現可能buildを最初に行う。
- WebRTC、Media Foundation、IPCの問題を分離してdebugできる構造にする。
- 仮想カメラが失敗してもアプリ内previewまで検証可能にする。
- VB-CABLEがなくてもspeakerでaudio受信を検証可能にする。

## 2. Milestone 0: Repository/Toolchain

成果:

- repository skeleton
- docs配置
- Cloud package lock
- Windows build skeleton
- dependency policy
- libwebrtc revision selection
- build scripts
- CI skeleton
- `IMPLEMENTATION_STATUS.md`
- `KNOWN_ISSUES.md`
- `third_party/webrtc-lock.json`

完了条件:

- cloud test commandが動く
- Windows hello app/testがbuild
- libwebrtc static libraryが再現build
- secretsなし

## 3. Milestone 1: Cloud signaling

成果:

- Worker routes
- Session Durable Object
- REST API
- TTL Alarm
- token/HMAC
- static placeholder sender page
- unit tests
- staging deploy

完了条件:

- curl等でcreate→claim→offer→answer→delete
- double claim/expiry test
- no-store/security headers
- `wrangler deploy --env staging`

この段階ではSDPをdummy stringとしてAPI確認可能。

## 4. Milestone 2: Browser sender

成果:

- QR fragment parser
- getUserMedia
- local preview
- claim
- Offer
- ICE complete
- Offer PUT
- Answer polling
- state/error UI
- Playwright tests

完了条件:

- temporary browser/native test peerまたはfixtureでOffer生成を確認
- Token URL cleanup
- Stop cleanup
- mobile browser manual test

## 5. Milestone 3: Windows WebRTC receiver

成果:

- Win32 UI
- Session create
- QR render
- Offer poll
- native libwebrtc Answer
- D3D/GDI preview
- audio default endpoint
- stats
- clean shutdown

完了条件:

- Browser→Windows video/audio
- 10分連続
- VP8/Opus
- H.264 status記録
- QR end-to-end

この段階ではVirtual Camera/VB-CABLE固有機能なしでもよい。

## 6. Milestone 4: VB-CABLE routing

成果:

- render endpoint enumeration
- endpoint selection
- config persistence
- CABLE Input auto-detect
- fallback speaker
- device loss handling

完了条件:

- downstream appでCABLE Outputをmicrophoneとして受信
- no double playout
- VB-CABLEなしでgraceful

## 7. Milestone 5: Virtual Camera

成果:

- Media Source DLL
- dev installer/register scripts
- Session Virtual Camera Controller
- named pipe protocol
- Receiver frame conversion
- pipe publisher
- Media Source receiver
- 720p30 NV12
- Camera/OBS integration

完了条件:

- remote videoがVirtual Cameraへ表示
- consumer open/close
- Receiver exitでcamera消失
- malformed pipe test
- no unbounded frame queue

## 8. Milestone 6: Hardening/Packaging

成果:

- error handling
- structured logs/redaction
- diagnostics export
- TURN integration
- rate limiting
- installer strategy
- license notices
- full tests
- 30分soak
- operator docs

完了条件:

- acceptance checklist
- staging→production deploy
- repeatable clean-machine install
- known limitations
- cleanup/uninstall

## 9. Suggested commit boundaries

```text
chore: initialize repository and build tooling
feat(cloud): add durable-object signaling session
test(cloud): cover signaling state and expiry
feat(web): add browser sender and non-trickle offer
feat(win): add native webrtc receiver session
feat(win): add video preview and stats
feat(audio): route playout to selected endpoint
feat(vcam): add media source and registration
feat(vcam): add bounded named-pipe frame transport
test(e2e): validate browser to virtual camera path
chore: add packaging, redaction, and release docs
```

Agentがcommitを作成する権限を持たない場合でも、この粒度で作業を分離する。

## 10. Definition of Done for each milestone

- build commandがREADMEにある
- automated tests
- no new unbounded queue
- no secrets
- error paths
- cleanup paths
- dependency/license更新
- `IMPLEMENTATION_STATUS.md`更新
- design deviationはADR
- manual test steps
- warnings zeroまたは説明
- no placeholder/TODO in critical path

## 11. PoC後の低遅延Phase

初期成果を壊さず別phaseで実施する。

候補:

1. segmentごとのlatency計測
2. browser codec preference
3. Windows hardware decode
4. iOS VideoToolbox H.264 sender
5. frame queue削減
6. preview/VSync改善
7. pipe→shared memory/D3D shared texture
8. audio buffer調整
9. Trickle ICE
10. direct UDP/TURN policy計測

低遅延Phase前にbaselineのglass-to-glass、audio latency、CPU、packet lossを測定する。
