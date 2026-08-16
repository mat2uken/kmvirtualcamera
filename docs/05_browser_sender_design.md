# 05. ブラウザ送信ページ設計

## 1. 技術選択

- Vanilla TypeScript
- HTML/CSS
- Browser標準WebRTC API
- buildはVite等の小さなbundler、またはTypeScript compiler
- UI frameworkなし
- 外部CDN scriptなし
- Service Workerなし
- Cookieなし
- Analyticsなし

依存を抑え、CSPを`'self'`だけで構成する。

## 2. 画面

### 初期表示

- 製品/PoC名
- 接続先Sessionの一部
- 有効期限
- カメラ選択
- マイク選択
- ローカルプレビュー
- 「送信開始」
- エラー/状態
- privacy説明

### 送信中

- 接続状態
- camera/mic on/off表示
- 実際の送信解像度
- 接続種別の簡易表示
- 「停止」
- 画面を閉じないよう案内

### 終了

- 送信終了
- 新しいQRコードが必要であること
- 自動retryを無限に行わない

## 3. State machine

```text
BOOT
  → INVALID_LINK
  → READY
  → REQUESTING_MEDIA
  → CLAIMING
  → CREATING_OFFER
  → UPLOADING_OFFER
  → WAITING_ANSWER
  → CONNECTING
  → CONNECTED
  → STOPPING
  → CLOSED

任意の処理
  → ERROR
```

各state transitionで以前のAbortControllerやTimerをcleanupする。

## 4. URL fragment

期待形式:

```text
#v=1&s=<sessionId>&j=<joinToken>
```

実装:

```ts
const params = new URLSearchParams(location.hash.slice(1));
const version = params.get("v");
const sessionId = params.get("s");
const joinToken = params.get("j");
```

検証後:

```ts
sessionStorage.setItem("wrtc.sessionId", sessionId);
sessionStorage.setItem("wrtc.joinToken", joinToken);
history.replaceState(null, "", location.pathname);
```

- DOMへJoin Tokenを表示しない。
- console logへ出さない。
- error reporting payloadへ含めない。
- claim後にJoin Tokenを削除する。

## 5. メディア取得

ユーザー操作のclick handlerから開始する。

初期constraints:

```ts
const constraints: MediaStreamConstraints = {
  video: {
    width: { ideal: 1280 },
    height: { ideal: 720 },
    frameRate: { ideal: 30, max: 30 },
    facingMode: { ideal: "environment" }
  },
  audio: true
};
```

注意:

- `exact`を多用せず、端末互換性を優先する。
- iOS Safariではdevice selectionやlabel取得に制約があるため、機能検出する。
- device labelはpermission取得後に得られる場合がある。
- front camera previewだけをCSSでmirrorしても、送信映像自体をmirrorしない。
- local preview videoは`muted`, `playsInline`, `autoplay`。

オーディオ処理はv1でbrowser defaultとする。後から「音声処理なし」presetを追加する場合は、`echoCancellation`、`noiseSuppression`、`autoGainControl`を明示する。

## 6. claim順序

推奨:

1. link検証
2. ユーザーStart
3. getUserMedia成功
4. claim
5. PeerConnection作成

理由: permission拒否だけでSessionをclaim済みにしない。

claim前にSessionが期限切れの場合は、取得したMediaStream trackを直ちにstopする。

## 7. RTCPeerConnection

```ts
const pc = new RTCPeerConnection(rtcConfiguration);
```

各track:

```ts
const transceiver = pc.addTransceiver(track, {
  direction: "sendonly",
  streams: [stream]
});
```

単純に`addTrack()`してもよいが、directionを明確にするためtransceiverを推奨する。

### Codec preference

v1は無理なSDP mungingを行わない。

- VP8とH.264のfallbackを保持する。
- `RTCRtpSender.getCapabilities("video")`と`setCodecPreferences()`が利用可能な場合だけ、設定でH.264を先頭へ移動してよい。
- H.264だけに絞らない。
- RTX/RED等、browserが必要とする関連codecを誤って除外しない。
- codec preferenceが失敗した場合はdefault negotiationへ戻す。

AudioはOpusを利用するが、v1ではSDP文字列編集で強制しない。

## 8. Offer生成

```ts
const offer = await pc.createOffer();
await pc.setLocalDescription(offer);
await waitForIceGatheringComplete(pc, ICE_GATHER_TIMEOUT_MS, signal);

const local = pc.localDescription;
if (!local || local.type !== "offer") throw ...;

await api.putOffer(sessionId, senderToken, {
  type: local.type,
  sdp: local.sdp
});
```

ICE timeoutの推奨初期値は15秒。Session全体timeoutは60秒。

## 9. Answer polling

- 最初の15秒: 1000ms
- 以降: 2000ms
- 合計60秒
- `Retry-After`が返れば上限内で尊重
- visibility changeでpollを止めないが、tab close/StopでAbort
- 204以外のretryable responseのみ限定回数retry
- 401/403/409/410は即時停止

Answer受信:

```ts
await pc.setRemoteDescription(answer);
```

その後、`connectionState`/`iceConnectionState`を監視する。

## 10. 状態イベント

監視:

- `icegatheringstatechange`
- `iceconnectionstatechange`
- `connectionstatechange`
- `signalingstatechange`
- `track.onended`
- local track `ended`
- `navigator.mediaDevices.devicechange`（対応時）

UIへ生の内部enumをそのまま表示せず、利用者向け文言へ変換する。診断panelでは内部値を表示してよい。

## 11. getStats

接続後2秒間隔程度で最低限取得:

- outbound-rtp video/audio
- codec
- candidate-pair
- local-candidate
- remote-candidate
- framesEncoded
- frameWidth/frameHeight
- framesPerSecond
- bytesSent
- packetsSent
- retransmittedPacketsSent
- totalEncodeTime
- qualityLimitationReason

PoC UIへ全項目を出す必要はない。診断JSONのexportは秘密情報を含めないようにする。

## 12. Stop

Stop操作:

1. polling Abort
2. stats timer停止
3. RTCPeerConnection.close()
4. 全MediaStreamTrack.stop()
5. video `srcObject=null`
6. best-effort Session DELETE
7. sessionStorageのToken削除
8. stateをCLOSEDへ

`beforeunload`内のfetchは保証されない。`sendBeacon`ではAuthorization headerを付けられないため、DELETEを必須とせずTTLを最終手段とする。

## 13. エラー表示

分類:

| Error | 表示例 |
|---|---|
| Invalid link | QRコードが無効です |
| Expired | 接続コードの有効期限が切れました |
| Already claimed | このQRコードはすでに使用されています |
| Permission denied | カメラまたはマイクの利用が許可されませんでした |
| Device not found | 利用可能なカメラ/マイクがありません |
| HTTPS/security | 安全なHTTPSページで開いてください |
| Signaling timeout | Windows側から応答がありません |
| ICE failed | ネットワーク経路を確立できませんでした |
| API unavailable | シグナリングサービスへ接続できません |

technical detail、request ID、browser versionを折りたたみ診断へ表示する。

## 14. Browser test

### Unit

- fragment parser
- token redaction
- polling backoff
- Abort
- ICE completion helper
- API error mapping
- codec preference helper

### Playwright

fake media flagsが使えるChromiumで:

- valid link
- claim
- permission flow
- Offer PUT
- Answer polling
- timeout
- Stop cleanup
- no Token in URL after parse
- CSP violationなし

### Manual

- Chrome Windows/Android
- Edge Windows
- Safari iOS
- rotation
- front/back camera
- network switch
- screen lock/visibility
