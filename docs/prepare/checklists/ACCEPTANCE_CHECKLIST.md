# PoC受入チェックリスト

## Cloudflare

- [ ] Worker Static AssetsとAPIが同一deployment
- [ ] `/send/`がHTTPSで配信
- [ ] SQLite-backed Durable Object migration
- [ ] Session作成
- [ ] Join/Receiver/Sender Token分離
- [ ] Join TokenはURL fragment
- [ ] 一度だけclaim
- [ ] 同一claimNonce retry
- [ ] Offer/Answer state machine
- [ ] Non-Trickle candidate-complete SDP
- [ ] poll未準備は204
- [ ] 60秒poll timeout
- [ ] fixed 5分TTL
- [ ] pollでTTL非延長
- [ ] Alarm cleanup
- [ ] body/SDP size limit
- [ ] no-store
- [ ] CSP/Permissions-Policy/Referrer-Policy
- [ ] Token/SDP/TURN credential非log
- [ ] TURN secret server-side
- [ ] TURN port 53 filter
- [ ] unit tests成功
- [ ] staging deploy成功

## Browser Sender

- [ ] QR URL parse
- [ ] fragment削除
- [ ] localStorage/cookie不使用
- [ ] Start user gesture
- [ ] getUserMedia camera/mic
- [ ] local muted preview
- [ ] permission error
- [ ] getUserMedia後claim
- [ ] sendonly tracks
- [ ] Offer create/setLocal
- [ ] ICE gathering complete待ち
- [ ] final localDescription PUT
- [ ] Answer polling
- [ ] setRemoteDescription
- [ ] connection state
- [ ] Stop cleanup
- [ ] VP8 fallback
- [ ] H.264 onlyでない
- [ ] Playwright成功
- [ ] Chrome/Edge manual
- [ ] mobile browser manual

## Windows Receiver

- [ ] C++20 x64 build
- [ ] fixed libwebrtc commit
- [ ] toolchain/GN args記録
- [ ] Session create
- [ ] QR local generation
- [ ] Join URL copy
- [ ] Offer polling
- [ ] SetRemoteDescription
- [ ] Answer create/setLocal
- [ ] ICE gathering complete待ち
- [ ] candidate-complete Answer PUT
- [ ] VP8/Opus receive
- [ ] H.264 result記録
- [ ] video preview
- [ ] rotation
- [ ] aspect-fit/letterbox
- [ ] I420→NV12
- [ ] latest-only frame processing
- [ ] no unbounded queue
- [ ] stats/diagnostics
- [ ] HTTPS validation有効
- [ ] cancellation
- [ ] clean shutdown

## Audio/VB-CABLE

- [ ] render endpoint列挙
- [ ] stable ID優先
- [ ] CABLE Input検出
- [ ] user選択
- [ ] VB-CABLEなしfallback
- [ ] CABLE Outputをdownstream appで確認
- [ ] 二重再生なし
- [ ] device loss handling
- [ ] 30分audio continuity

## Virtual Camera

- [ ] Windows 11 build 22000+ check
- [ ] Media Source DLL build
- [ ] COM registration script
- [ ] uninstall script
- [ ] Media Source libwebrtc非依存
- [ ] Session lifetime
- [ ] CurrentUser access
- [ ] MFCreateVirtualCamera background call
- [ ] Start/Stop/Shutdown
- [ ] 720p30 NV12
- [ ] monotonic sample time
- [ ] named pipe explicit serialization
- [ ] pipe ACL
- [ ] exact size validation
- [ ] partial read/write
- [ ] malformed data test
- [ ] reader background thread
- [ ] RequestSample non-blocking
- [ ] latest frame
- [ ] stale→black
- [ ] Camera app
- [ ] OBS
- [ ] browser camera selector
- [ ] repeated open/close
- [ ] Receiver exitでcamera消失

## Security/Operations

- [ ] no secrets committed
- [ ] no full libwebrtc checkout committed
- [ ] third-party licenses
- [ ] binary install path protected
- [ ] structured logs
- [ ] redaction tests
- [ ] log rotation
- [ ] request ID
- [ ] rate limit or documented staging restriction
- [ ] 30-minute soak
- [ ] memory/handles stable
- [ ] clean-machine build/install procedure
- [ ] known issues
- [ ] implementation status
- [ ] design deviations documented
