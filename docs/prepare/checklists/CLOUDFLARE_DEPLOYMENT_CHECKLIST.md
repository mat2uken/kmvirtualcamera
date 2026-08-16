# Cloudflareデプロイチェックリスト

## Account/Project

- [ ] Workers project作成
- [ ] account ID確認
- [ ] staging environment
- [ ] production environment
- [ ] custom domainまたはworkers.dev
- [ ] Static Assets設定
- [ ] `/v1/*` worker-first route

## Durable Objects

- [ ] `SESSIONS` binding
- [ ] `SessionDurableObject` class
- [ ] SQLite-backed migration tag
- [ ] local test
- [ ] staging migration
- [ ] Alarm test
- [ ] storage cleanup test

## Variables

- [ ] `PUBLIC_BASE_URL`
- [ ] `SESSION_TTL_SECONDS=300`
- [ ] `SIGNALING_TIMEOUT_SECONDS=60`
- [ ] `ENABLE_TURN`

## Secrets

- [ ] `TOKEN_HMAC_SECRET`
- [ ] `TURN_KEY_ID` optional
- [ ] `TURN_KEY_API_TOKEN` optional
- [ ] no secret in wrangler config
- [ ] no secret in repository
- [ ] secret rotation procedure

## TURN

- [ ] TURN key作成
- [ ] short credential generation
- [ ] TTL
- [ ] port 53 filter
- [ ] UDP 3478
- [ ] TCP/TLS 443 fallback
- [ ] pricing/quota確認
- [ ] relay test
- [ ] credentials non-log

## Security Headers

- [ ] CSP
- [ ] Permissions-Policy
- [ ] Referrer-Policy
- [ ] X-Content-Type-Options
- [ ] frame-ancestors none
- [ ] no-store on API
- [ ] no wildcard CORS
- [ ] HTTPS redirect

## API

- [ ] OpenAPI一致
- [ ] request size
- [ ] auth roles
- [ ] one-time claim
- [ ] fixed TTL
- [ ] 204 polling
- [ ] request ID
- [ ] errors
- [ ] rate limiting
- [ ] logs redacted

## Browser Assets

- [ ] `/send/`
- [ ] no third-party CDN
- [ ] no inline script
- [ ] source map production policy
- [ ] QR fragment parse
- [ ] camera/mic permission
- [ ] mobile test

## Testing/Monitoring

- [ ] `npm ci`
- [ ] unit tests
- [ ] build
- [ ] staging smoke
- [ ] create/claim/offer/answer/delete
- [ ] expiry
- [ ] usage dashboard
- [ ] free quota/limit確認
- [ ] error alerts
- [ ] rollback command
