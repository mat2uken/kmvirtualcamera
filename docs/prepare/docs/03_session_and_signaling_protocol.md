# 03. セッションとシグナリングプロトコル

詳細なHTTP schemaは`specs/openapi.yaml`を正とする。

## 1. 基本原則

- すべてHTTPS。
- Static sender pageとAPIは同一オリジン。
- SDPと短命Tokenだけを交換する。
- Worker/DOはSDPを解釈・書き換えしない。
- Join TokenはQR URLのfragmentへ入れる。
- API認証は`Authorization: Bearer ...`。
- Offer/AnswerのbodyはJSON。
- poll結果未準備時は`204 No Content`。
- responseは`Cache-Control: no-store`。
- セッションは作成から5分で必ず期限切れ。pollでは延長しない。
- 接続完了後はAPIを継続pollしない。

## 2. QR Join URL

形式:

```text
https://<host>/send/#v=1&s=<session-id>&j=<join-token>
```

fragmentを使う理由:

- URL fragmentは通常のHTTP request targetへ送信されない。
- CDN/Worker access logへJoin Tokenが入りにくい。
- Refererへ含まれにくくするため、ページ側も`Referrer-Policy: no-referrer`を指定する。

Sender pageは起動直後に以下を行う。

1. fragmentから`v`、`s`、`j`を読む。
2. 値の形式を検証する。
3. `sessionStorage`へ一時保存する。
4. `history.replaceState()`でfragmentをアドレスバーから削除する。
5. claim成功後はJoin TokenをsessionStorageから削除する。
6. Sender Tokenだけを当該tabのsessionStorageへ保持する。
7. localStorage、cookie、IndexedDBへTokenを保存しない。

## 3. Token

| Token | 利用者 | 用途 |
|---|---|---|
| Join Token | 未claimのSender | Sessionを一度だけclaim |
| Receiver Token | Windows Receiver | Offer取得、Answer登録、削除 |
| Sender Token | claim済みSender | Offer登録、Answer取得、削除 |

要件:

- 32 random bytes以上をbase64url化
- URL-safe、paddingなし
- token比較はconstant-timeを意識
- storageには原則SHA-256 digestを保存
- token本文をlogへ出さない
- Session IDだけでは操作不可

### claimの冪等性

ブラウザは16 random bytes以上の`claimNonce`を生成し、sessionStorageへ保持する。

最初のclaim:

- Join Tokenを検証
- claimNonce hashを保存
- Sender Tokenを発行
- stateを`CLAIMED`へ変更

同じclaimNonceで再送:

- 同一Senderからのretryとみなし、同じSender Tokenを再現可能にする
- 推奨実装はWorker secretによるHMACからSender Tokenを決定的に導出する
- 異なるclaimNonceは`409 SESSION_ALREADY_CLAIMED`

## 4. APIフロー

### 4.1 Session作成

```http
POST /v1/sessions
Content-Type: application/json
```

request例:

```json
{
  "client": {
    "name": "windows-receiver",
    "version": "0.1.0"
  }
}
```

response例:

```json
{
  "sessionId": "Fh9r8DRR7c9tE7Jb7nD2Aw",
  "receiverToken": "<secret>",
  "joinUrl": "https://example.workers.dev/send/#v=1&s=Fh9...&j=<secret>",
  "expiresAt": "2026-08-16T03:05:00.000Z",
  "poll": {
    "initialIntervalMs": 1000,
    "backoffAfterMs": 15000,
    "maxIntervalMs": 2000,
    "timeoutMs": 60000
  },
  "rtcConfiguration": {
    "iceServers": [
      {
        "urls": ["stun:stun.cloudflare.com:3478"]
      }
    ],
    "iceTransportPolicy": "all"
  }
}
```

### 4.2 claim

```http
POST /v1/sessions/{sessionId}/claim
Authorization: Bearer {joinToken}
Content-Type: application/json
```

```json
{
  "claimNonce": "N_4wJkWzKJk3...",
  "client": {
    "name": "browser-sender",
    "version": "0.1.0"
  }
}
```

response:

```json
{
  "senderToken": "<secret>",
  "expiresAt": "2026-08-16T03:05:00.000Z",
  "poll": {
    "initialIntervalMs": 1000,
    "backoffAfterMs": 15000,
    "maxIntervalMs": 2000,
    "timeoutMs": 60000
  },
  "rtcConfiguration": {
    "iceServers": [
      {
        "urls": ["stun:stun.cloudflare.com:3478"]
      }
    ],
    "iceTransportPolicy": "all"
  }
}
```

### 4.3 Offer登録

```http
PUT /v1/sessions/{sessionId}/offer
Authorization: Bearer {senderToken}
Content-Type: application/json
```

```json
{
  "type": "offer",
  "sdp": "v=0\r\n..."
}
```

成功: `204 No Content`

制約:

- stateは`CLAIMED`
- typeは`offer`
- UTF-8 JSON全体およびSDPに上限
- 同一内容のretryは冪等成功としてよい
- 異なる2つ目のOfferは`409 INVALID_STATE`

### 4.4 Offer取得

```http
GET /v1/sessions/{sessionId}/offer
Authorization: Bearer {receiverToken}
```

未準備:

```http
204 No Content
Retry-After: 1
```

準備済み:

```json
{
  "type": "offer",
  "sdp": "v=0\r\n..."
}
```

### 4.5 Answer登録

```http
PUT /v1/sessions/{sessionId}/answer
Authorization: Bearer {receiverToken}
Content-Type: application/json
```

成功: `204 No Content`

### 4.6 Answer取得

```http
GET /v1/sessions/{sessionId}/answer
Authorization: Bearer {senderToken}
```

未準備時は204、準備済みはAnswer JSON。

### 4.7 削除

```http
DELETE /v1/sessions/{sessionId}
Authorization: Bearer {receiverToken or senderToken}
```

成功: `204 No Content`

DELETEはbest effort。失敗しても期限切れAlarmが最終削除を行う。

## 5. 状態機械

```text
ABSENT
  │ create
  ▼
CREATED
  │ claim(join token)
  ▼
CLAIMED
  │ put offer(sender token)
  ▼
OFFER_READY
  │ put answer(receiver token)
  ▼
ANSWER_READY
  │ delete / alarm
  ▼
CLOSED or EXPIRED
```

許可されないtransitionには`409 Conflict`。

poll GETはstateを変えない。

## 6. Receiverフロー

```text
POST session
  → QR表示
  → poll GET offer
  → PeerConnection.SetRemoteDescription(offer)
  → CreateAnswer
  → SetLocalDescription(answer)
  → wait ICE gathering complete
  → PUT answer
  → WebRTC state監視
  → connected後、signaling poll停止
```

Offer pollとSession expirationの両方を監視する。

## 7. Senderフロー

```text
QRでpage起動
  → fragment parse/remove
  → ユーザーがStart
  → getUserMedia
  → claim
  → RTCPeerConnection(rtcConfiguration)
  → trackをsendonlyで追加
  → CreateOffer
  → SetLocalDescription
  → wait ICE gathering complete
  → PUT offer
  → poll GET answer
  → SetRemoteDescription(answer)
  → connection state監視
```

getUserMedia成功後にclaimすることで、permission拒否だけでSessionをclaim済みにしない。

## 8. ICE gathering完了待ち

Browserの概念コード:

```ts
async function waitForIceGatheringComplete(
  pc: RTCPeerConnection,
  timeoutMs: number,
  signal: AbortSignal
): Promise<void> {
  if (pc.iceGatheringState === "complete") return;

  await Promise.race([
    new Promise<void>((resolve, reject) => {
      const onChange = () => {
        if (pc.iceGatheringState === "complete") {
          cleanup();
          resolve();
        }
      };
      const onAbort = () => {
        cleanup();
        reject(new DOMException("Aborted", "AbortError"));
      };
      const cleanup = () => {
        pc.removeEventListener("icegatheringstatechange", onChange);
        signal.removeEventListener("abort", onAbort);
      };
      pc.addEventListener("icegatheringstatechange", onChange);
      signal.addEventListener("abort", onAbort, { once: true });
    }),
    timeout(timeoutMs, signal)
  ]);
}
```

`pc.localDescription`を送るのは、完了を待った後とする。`createOffer()`が返した最初のSDPではなく、candidateが追加された`localDescription`を使う。

Windows/libwebrtc側もGatheringState callbackを待ち、local descriptionの最終SDPを取得する。

## 9. HTTP status

| Status | 意味 |
|---:|---|
| 200 | JSONデータあり |
| 201 | Session作成 |
| 204 | 成功bodyなし、またはpoll対象未準備 |
| 400 | schema/format不正 |
| 401 | Bearer Tokenなし・形式不正 |
| 403 | Token不一致 |
| 404 | route/session不明 |
| 409 | state conflict、claim済み |
| 410 | Session期限切れ |
| 413 | body/SDP過大 |
| 415 | Content-Type不正 |
| 429 | rate limit |
| 500 | 内部エラー |
| 502/503 | TURN credential等の外部依存失敗 |

エラーbody:

```json
{
  "error": {
    "code": "SESSION_EXPIRED",
    "message": "The signaling session has expired.",
    "retryable": false,
    "requestId": "..."
  }
}
```

利用者向け画面では内部情報を出しすぎず、診断画面にrequest IDを表示する。

## 10. 再接続

v1はICE Restartと再Offerを行わない。

- failedになったら現在のPeerConnectionをclose
- SessionをDELETE
- Receiverが新Session/QRを作成
- Senderは再スキャンまたは新URLを開く

この制約によりrevision管理と複数SDP generationを初期実装から除外する。
