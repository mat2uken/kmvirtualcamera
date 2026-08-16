# 10. テスト計画と受入基準

## 1. テスト層

```text
Unit
  ├─ Cloudflare state/auth/validation
  ├─ Browser utilities/state
  └─ Windows protocol/conversion/state

Component
  ├─ Worker + Durable Object
  ├─ Browser + mocked signaling
  ├─ Windows + mocked signaling
  ├─ Native libwebrtc loop/interoperability
  └─ Virtual Camera Media Source + test harness

Integration
  ├─ Browser ↔ Cloudflare ↔ Windows
  ├─ WebRTC media
  ├─ Windows ↔ VB-CABLE
  └─ Windows ↔ Virtual Camera ↔ consumer app

Manual E2E
  └─ QR scan through downstream camera/mic consumer
```

## 2. Cloudflare unit tests

| ID | Test |
|---|---|
| CF-001 | Session create returns valid IDs/tokens/fragment URL |
| CF-002 | Session expiration fixed at creation |
| CF-003 | poll does not extend expiry |
| CF-004 | claim succeeds once |
| CF-005 | same claimNonce retry is idempotent |
| CF-006 | different claimNonce is rejected |
| CF-007 | receiver token cannot PUT offer |
| CF-008 | sender token cannot PUT answer |
| CF-009 | GET before data returns 204 |
| CF-010 | Offer then Answer valid transition |
| CF-011 | Answer before Offer rejected |
| CF-012 | identical PUT retry succeeds |
| CF-013 | conflicting second PUT rejected |
| CF-014 | expired Session returns 410 |
| CF-015 | alarm deletes storage |
| CF-016 | malformed JSON rejected |
| CF-017 | wrong Content-Type rejected |
| CF-018 | oversized SDP rejected |
| CF-019 | security/no-store headers |
| CF-020 | logs redact secrets |
| CF-021 | TURN long-term secret never returned |
| CF-022 | port 53 ICE URLs filtered in non-trickle config |

## 3. Browser tests

### Unit

- fragment parse
- history replacement
- sessionStorage lifecycle
- claim retry nonce
- API response mapping
- polling backoff
- Abort
- ICE gathering completion
- timeout
- codec preference fallback
- Stop cleanup
- redaction

### Playwright

Chromium fake media:

- valid QR URL
- Start gesture
- getUserMedia
- claim request
- Offer PUT
- Answer polling 204→200
- state transition
- Stop
- expired link
- already claimed
- API down
- permission denied simulation
- no fragment after initialization
- no secret in console/network URL
- CSP no violation

## 4. Windows unit tests

| Module | Tests |
|---|---|
| DTO/API | valid/invalid JSON、size、HTTP status |
| Polling | timeout、cancel、backoff、204 |
| State machine | all valid/invalid transitions |
| Token holder | no accidental copy/log |
| QR | URL→matrix、DPI render |
| Frame conversion | I420 rotation/scale/letterbox/NV12 |
| Latest frame | producer/consumer race、drop semantics |
| Pipe protocol | header validation、partial read、oversize、disconnect |
| Audio device | enumerate/match/fallback |
| Config | defaults、invalid values |
| Logging | redaction、rotation |

Sanitizer相当、static analysis、warningsを有効化する。MSVC `/W4`、可能な範囲で`/WX`。

## 5. Native WebRTC interop tests

- Browser Chrome → Windows VP8/Opus
- Browser Edge → Windows VP8/Opus
- Browser H.264 preference → Windows H.264/Opus
- Offer contains sendonly
- Answer contains recvonly/inactive as expected
- Non-Trickle SDP has ICE candidates
- selected candidate pair取得
- STUN direct
- TURN UDP
- TURN TLS 443
- camera mute/unmute
- mic mute/unmute
- track end
- tab close
- network unplug
- connection failed

## 6. Virtual Camera tests

- DLL load test
- Media Source create/shutdown
- supported media type exactly one
- black frame without producer
- valid pipe frame
- partial header
- invalid magic/version
- invalid width/height/fourcc
- payload short/long
- writer disconnect/reconnect
- 30fps timestamps monotonic
- latest frame repeated
- no deadlock on RequestSample
- consumer open/close 20回
- multiple consumer application
- session camera disappears after Receiver exit
- uninstall cleanup

Consumer matrix:

- Windows Camera
- OBS Studio
- Chrome camera test page
- Edge camera test page
- Teams/Zoom where available

## 7. VB-CABLE tests

- not installed
- installed
- CABLE Input selection
- downstream CABLE Output
- audio meter
- 48kHz
- mono/stereo
- device disable/re-enable
- endpoint change while connected
- 30-minute continuous
- no duplicate speaker output
- no feedback loop

## 8. Network matrix

| Sender | Receiver | TURN | Expected |
|---|---|---|---|
| Same LAN | Same LAN | off | direct host/srflx |
| Different home NAT | Internet | off | may succeed/fail |
| Different home NAT | Internet | on | succeed via direct or relay |
| UDP blocked Sender | Internet | TURN TLS 443 | connect if policy permits |
| UDP blocked Receiver | Internet | TURN TLS 443 | connect if policy permits |
| Captive portal | Any | any | fail with clear diagnostic |
| Network switch mid-call | Any | v1 | likely fail; require new Session |

## 9. Soak test

条件:

- 720p30 browser camera
- audio active
- Windows preview
- Virtual Camera opened by OBS/Camera app
- VB-CABLE selected
- 30分以上

記録:

- Receiver RSS/commit
- CPU/GPU
- handle count
- thread count
- frames received
- frames converted
- pipe frames sent/dropped
- virtual camera samples
- audio underrun
- WebRTC stats
- Worker request count
- connection state transitions

受入:

- crashなし
- handle/threadの継続増加なし
- queue増加なし
- memoryはwarm-up後に概ね安定
- mediaが継続
- downstream camera/mic利用可能

## 10. E2E受入シナリオ

### E2E-001 基本接続

1. Receiver起動。
2. Session作成成功。
3. QR表示。
4. Android Chrome等でscan。
5. permission許可。
6. Start。
7. Windows preview表示。
8. audio signal表示。
9. Virtual CameraをCamera appで選択。
10. 同じremote映像確認。
11. VB-CABLE CABLE Outputを録音アプリで選択。
12. remote audio確認。
13. Stop。
14. Session cleanup。

### E2E-002 期限切れ

1. QR表示後5分待つ。
2. Browserで開く/Start。
3. expired表示。
4. ReceiverでNew Session可能。

### E2E-003 二重scan

1. Browser Aがclaim。
2. Browser Bが同QRでStart。
3. Bはalready used。
4. Aの接続は影響なし。

### E2E-004 Sender異常終了

1. connected。
2. tab force close。
3. Receiverがdisconnected/failedを表示。
4. Virtual Cameraは一定時間後black。
5. 新Session作成可能。

### E2E-005 Camera consumerなし/後から起動

1. WebRTC接続。
2. Virtual Camera consumerなし。
3. Receiverは正常。
4. 後からOBSでcamera選択。
5. 最新映像を表示。

## 11. Release gate

次が全て満たされるまでPoC releaseを作らない。

- [ ] Cloud unit test全成功
- [ ] Browser unit/Playwright全成功
- [ ] Windows unit test全成功
- [ ] Chrome→Windows VP8/Opus成功
- [ ] H.264 interop結果を記録
- [ ] QR flow成功
- [ ] Virtual Camera Camera app成功
- [ ] VB-CABLE downstream成功
- [ ] 30分soak成功
- [ ] Token/SDP log leakなし
- [ ] install/uninstall手順再現
- [ ] pinned dependency記録
- [ ] known issues更新
