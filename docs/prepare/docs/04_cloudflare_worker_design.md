# 04. Cloudflare Worker / Durable Object設計

## 1. 採用サービス

- Cloudflare Workers
- Worker Static Assets
- SQLite-backed Durable Objects
- Workers Secrets
- 任意: Cloudflare Realtime TURN

不採用:

- D1: Session状態には不要。将来の永続データ用。
- KV: 結果整合性のためSDP交換に使わない。
- R2: 短命な小データに不適切。
- WebSocket/SSE: 初期版はHTTPS pollingに限定。
- Calls/SFU: 1対1 P2P PoCには不要。

## 2. Worker routing

推奨route順序:

```text
/v1/*       → API Worker logic
/send/*     → Static Assets
/           → landing/static
その他      → static asset fallbackまたは404
```

Static AssetsとAPIを同一Worker deploymentに含める。これにより:

- Sender pageとAPIが同一origin
- CORS設定が不要
- CSPの`connect-src 'self'`で完結
- deploy単位が1つ
- QR URLのhostを1つに固定

## 3. Wrangler構成例

実装時は現行Wrangler schemaへ合わせること。概念例:

```jsonc
{
  "$schema": "./node_modules/wrangler/config-schema.json",
  "name": "webrtc-bridge-signaling",
  "main": "src/index.ts",
  "compatibility_date": "2026-08-16",
  "assets": {
    "directory": "./dist/public",
    "binding": "ASSETS",
    "run_worker_first": ["/v1/*"]
  },
  "durable_objects": {
    "bindings": [
      {
        "name": "SESSIONS",
        "class_name": "SessionDurableObject"
      }
    ]
  },
  "migrations": [
    {
      "tag": "v1",
      "new_sqlite_classes": ["SessionDurableObject"]
    }
  ],
  "vars": {
    "PUBLIC_BASE_URL": "https://example.workers.dev",
    "SESSION_TTL_SECONDS": "300",
    "SIGNALING_TIMEOUT_SECONDS": "60",
    "ENABLE_TURN": "false"
  }
}
```

Secrets:

```text
TOKEN_HMAC_SECRET
TURN_KEY_ID             optional
TURN_KEY_API_TOKEN      optional
```

## 4. Worker module構成

```text
cloud/
├── src/
│   ├── index.ts
│   ├── router.ts
│   ├── env.ts
│   ├── http/
│   │   ├── errors.ts
│   │   ├── headers.ts
│   │   ├── json.ts
│   │   └── request-id.ts
│   ├── signaling/
│   │   ├── session-api.ts
│   │   ├── session-types.ts
│   │   ├── tokens.ts
│   │   └── validation.ts
│   ├── durable/
│   │   └── session-object.ts
│   └── turn/
│       └── credentials.ts
├── public/
├── test/
├── package.json
├── package-lock.json
├── tsconfig.json
└── wrangler.jsonc
```

過剰なWeb frameworkは導入せず、route数が少ないため小さな明示的routerでよい。

## 5. Durable Objectのデータ

各Session Durable Objectは1レコード相当の状態だけを持つ。

```ts
type SessionState =
  | "CREATED"
  | "CLAIMED"
  | "OFFER_READY"
  | "ANSWER_READY"
  | "CLOSED";

interface SessionRecord {
  schemaVersion: 1;
  sessionId: string;
  state: SessionState;

  createdAtMs: number;
  expiresAtMs: number;

  receiverTokenHash: string;
  joinTokenHash: string;

  claimNonceHash?: string;
  senderTokenHash?: string;

  offer?: {
    type: "offer";
    sdp: string;
    digest: string;
    storedAtMs: number;
  };

  answer?: {
    type: "answer";
    sdp: string;
    digest: string;
    storedAtMs: number;
  };

  rtcConfiguration: RtcConfigurationDto;

  closedAtMs?: number;
  closedReason?: string;
}
```

Storage APIのtyped key-valueで`session` keyへ保存してよい。SQLite-backed classを使用するが、Sessionが1件だけなのでSQL tableは必須ではない。

## 6. 作成処理

Workerは次を行う。

1. request size/content type/schemaを検証。
2. Session IDを16 random bytes以上からbase64url生成。
3. Receiver Token、Join Tokenを32 random bytes以上から生成。
4. token digestを作る。
5. 必要ならTURN短期credentialを取得。
6. `env.SESSIONS.getByName(sessionId)`でstub取得。
7. internal create request/RPCでSessionRecordを一度だけ保存。
8. `expiresAt`時刻にAlarm設定。
9. Join URLを生成。
10. responseを返す。

同じSession IDが既に初期化済みなら内部エラーとし、別IDで再試行する。衝突確率は実質無視できるが、処理は明示する。

## 7. TTL

Alarmは作成時に一度だけ設定する。

```ts
await ctx.storage.setAlarm(expiresAtMs);
```

`fetch`やpollごとにTTLを延長しない。そうしないとpollや攻撃的requestでSessionが永久化する。

`alarm()`:

```ts
async alarm(): Promise<void> {
  await this.ctx.storage.deleteAll();
}
```

storageが存在しないSessionへの操作は`410 SESSION_EXPIRED`または`404 SESSION_NOT_FOUND`。Session IDの形式が正しく、かつ一度存在していたかを外部から完全には区別しなくてもよい。情報漏えいを抑えるなら両方を`410`へ統一してもよい。

## 8. Token実装

### Random token

Web Crypto:

```ts
const bytes = crypto.getRandomValues(new Uint8Array(32));
const token = base64UrlEncode(bytes);
```

### Digest

```ts
const digest = await crypto.subtle.digest(
  "SHA-256",
  new TextEncoder().encode(token)
);
```

比較時はbyte列を一定長で比較する。JavaScriptでは厳密なconstant-time保証は難しいが、長さを先に固定し、単純な文字列早期returnを避ける。

### Sender Tokenの冪等導出

```text
HMAC-SHA256(
  TOKEN_HMAC_SECRET,
  "sender-token-v1" || 0x00 || sessionId || 0x00 || claimNonce
)
```

結果をbase64url化する。同一claimNonceのretryは同じTokenになる。DOにはclaimNonce hashとSender Token hashだけを保存する。

## 9. TURN credential

`ENABLE_TURN=true`かつSecretsが設定されている場合だけ、WorkerがCloudflare Realtime TURN APIへserver-side requestを行う。

概念:

```http
POST https://rtc.live.cloudflare.com/v1/turn/keys/{TURN_KEY_ID}/credentials/generate-ice-servers
Authorization: Bearer {TURN_KEY_API_TOKEN}
Content-Type: application/json

{"ttl": 600}
```

要件:

- 長期TURN key/API tokenをclientへ渡さない。
- credential TTLはSession TTLより少し長い600秒程度。
- port 53のSTUN/TURN URLはNon-Trickle ICEの初期版では除外する。
- `turn:...:3478?transport=udp`を先に置く。
- TCP/TLS 443をfallbackとして残す。
- API失敗時のポリシーを設定化する。

推奨初期ポリシー:

```text
TURN未設定:
  STUNのみ

TURN設定済みでcredential生成成功:
  STUN + TURN

TURN設定済みでcredential生成失敗:
  Session作成を503で失敗させる
```

曖昧なSTUN-only fallbackより、運用設定不良を早く検出する方を推奨する。開発環境では明示的に`ENABLE_TURN=false`とする。

## 10. Validation

### Session ID

- base64url
- 20〜32文字程度
- path traversal文字なし

### Token

- Authorization Bearer
- 最大256文字
- base64urlのみ
- query parameterでは受け取らない

### JSON

- `Content-Type: application/json`
- request body上限をstream読込前後で検査
- unknown fieldを許可するかはschemaごとに明示
- SDPはstring、typeは固定enum
- NULを拒否
- SDP上限131072 bytes
- Offer bodyは`type="offer"`、Answerは`type="answer"`

WorkerはSDP grammarを完全parseしない。最低限:

- `v=0`で始まること
- CRLF/LFを許容
- media sectionが少なくとも1つ
- script/HTMLとして反射しない
- response Content-Typeを常にJSON

## 11. Response headers

API:

```text
Cache-Control: no-store, max-age=0
Pragma: no-cache
X-Content-Type-Options: nosniff
Referrer-Policy: no-referrer
X-Request-ID: <id>
Content-Type: application/json; charset=utf-8
```

Static sender:

```text
Content-Security-Policy:
  default-src 'self';
  script-src 'self';
  style-src 'self';
  img-src 'self' blob:;
  media-src 'self' blob:;
  connect-src 'self';
  base-uri 'none';
  frame-ancestors 'none';
  form-action 'none'

Permissions-Policy:
  camera=(self), microphone=(self), geolocation=()

Referrer-Policy: no-referrer
X-Content-Type-Options: nosniff
```

inline script/styleを避け、CSPで`unsafe-inline`を不要にする。

## 12. Logging

許可:

- timestamp
- request ID
- route名
- status
- duration
- response size
- Session IDの先頭数文字またはhash
- state transition名
- TURN API成功/失敗種別

禁止:

- Authorization header
- Join/Receiver/Sender Token
- Offer/Answer SDP
- ICE username/credential
- TURN credential
- 完全なJoin URL
- raw IPの長期保存

## 13. Rate limiting

Core PoCと分けて実装してよいが、公開deploymentでは最低限必要。

段階:

1. Cloudflare側の利用可能なRate Limiting機能を現行契約で確認。
2. 利用できない場合、HMAC化したclient IPをkeyにした小さなRateLimit Durable Objectを追加。
3. 目安:
   - Session create: 20回/時/IP
   - claim: 30回/5分/IP
   - invalid token: より厳しく制限
4. `Retry-After`を返す。
5. raw IPをapplication log/storageへ残さない。

Rate limit実装をCore signaling stateへ混ぜ込まず、middlewareとして独立させる。

## 14. テスト

必須単体テスト:

- createのtoken長・URL fragment
- claim成功
- 同じnonceのclaim retry
- 異なるnonceの二重claim拒否
- auth roleの分離
- offer前answer拒否
- offer/answerの同一body retry
- 異なる2回目body拒否
- 204 polling
- expiry
- alarm delete
- over-size SDP
- malformed JSON
- no-store/security headers
- secret redaction
- TURN URL filter

Miniflare/Workers test poolでDurable Objectを含めて実行する。

## 15. Free tier上の考慮

HTTPS pollingはrequest数を消費する。成功接続ではpollを直ちに止める。

例:

- Session create: 1
- claim: 1
- offer PUT: 1
- offer polling: 数回
- answer PUT: 1
- answer polling: 数回
- delete: 1

通常は1接続あたり十数request程度に抑えられる。失敗時でも60秒で停止する。Workers Free plan等の上限は変更され得るため、deployment時に公式limitsを確認し、Cloudflare dashboardで使用量を監視する。
