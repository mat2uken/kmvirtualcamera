# 09. セキュリティとプライバシー

## 1. Threat model

初期PoCで想定する脅威:

- 推測されたSession IDへの不正操作
- QR画像またはJoin URLの漏えい
- Sessionの二重claim
- SDP/ICE Candidateからのnetwork情報漏えい
- Workerへの大量Session作成・poll
- TURN long-term keyの漏えい
- Browser pageへのscript injection
- ログ/クラッシュdumpへのToken・SDP混入
- ローカルIPCへの別processからのframe injection
- Media Source DLLの置換・不正登録
- Windows appが不正なSDP/JSON/pipe payloadを処理
- 古いSession/Tokenの再利用

初期PoCでは防御しない、または限定的:

- 端末所有者自身のadministrator権限攻撃
- OS/Browser/Cloudflare基盤の侵害
- 高度なtraffic analysis
- QRを物理的に撮影した第三者による先取りclaim
- media contentのend-to-end identity verification/SAS
- enterprise authentication/SSO

## 2. 秘密情報

| 情報 | 分類 | 保存 |
|---|---|---|
| Join Token | Secret | URL fragment→sessionStorage、claim後削除 |
| Receiver Token | Secret | Receiver memoryのみ |
| Sender Token | Secret | Browser sessionStorage |
| TURN username/credential | Secret | Session memory/短命DO state |
| TURN long-term key | Highly secret | Worker Secretのみ |
| TOKEN_HMAC_SECRET | Highly secret | Worker Secretのみ |
| Offer/Answer SDP | Sensitive | DOへ最大5分 |
| ICE Candidate IP | Sensitive | SDP内、最大5分 |
| Session ID | Non-secret identifier | logは短縮/hash |
| WebRTC stats | Diagnostic | local log、IPをredact |

## 3. QRの安全性

Join URLは認証情報を含む。

- QRを公開画面に長時間表示しない。
- Session TTLは5分。
- claim後にReceiver UIのQRへ「使用済み」を重ねる。
- claim後は2つ目のSenderを拒否。
- URLをclipboardへcopyする場合、警告を表示してもよい。
- Join Tokenはfragment。
- QR URLをtelemetryへ送らない。
- screenshot/物理撮影は防げない。

PoCでより強くする場合:

- Receiver側にSenderの短い確認コードを表示
- Browser側にも同じcodeを表示
- ユーザーが一致確認
- DataChannel上で確認情報を交換

これはv1対象外。

## 4. API authentication

- Bearer Tokenをroleごとに分離。
- Receiver TokenでOffer PUTは不可。
- Sender TokenでAnswer PUTは不可。
- Join Tokenはclaim以外に不可。
- tokenをquery parameterに置かない。
- token全文をerrorへ反射しない。
- `Authorization` headerの最大長を制限。
- digest比較。
- 401と403の使い分けで情報を出しすぎない。

## 5. Session lifecycle

- 作成から固定5分。
- pollで延長しない。
- answer交換後、接続成立時にDELETEを推奨。
- delete失敗でもAlarm。
- DO storage delete。
- backup/analyticsへSDPを転送しない。
- Worker exception loggingでrequest bodyをdumpしない。

## 6. Browser security

Security headers:

```text
Content-Security-Policy
Permissions-Policy
Referrer-Policy: no-referrer
X-Content-Type-Options: nosniff
frame-ancestors 'none'
```

- inline script禁止。
- third-party JS/CDN禁止。
- `innerHTML`へAPI errorを代入しない。
- DOM textは`textContent`。
- external linkを極力置かない。
- linkを置く場合`rel="noreferrer noopener"`。
- Session Tokenをservice worker/cacheへ渡さない。
- response/cacheはno-store。
- Browser error reportはredaction後。

## 7. SDP/ICE handling

SDPはuntrusted input。

- body size上限。
- SDP string length上限。
- NUL拒否。
- native libwebrtc以外で複雑なparseを行わない。
- logへ出さない。
- UIへ生表示しない。developer-only exportでも明示操作とwarning。
- local IPが含まれる可能性をprivacy noteへ記載。
- remote description設定失敗時のerrorを安全に表示。
- custom SDP mungingを避ける。

## 8. TURN

- long-term keyはWorker Secret。
- browser/Windowsへは短期credentialのみ。
- TTL 10分程度。
- WorkerからCloudflare APIへHTTPS。
- API responseをlogしない。
- client configを診断exportするときcredentialをredact。
- TURN relay利用時はCloudflareへmedia trafficが流れることをprivacy noticeへ記載。
- current pricing/quotaを運用前に確認。

## 9. Cloudflare abuse対策

- rate limit。
- request/body size。
- Session TTL。
- invalid token試行のcounter。
- 1 Session 1 Sender。
- route allowlist。
- method allowlist。
- no wildcard CORS。
- Worker free quotaのalert。
- custom domainのWAFを利用可能なら設定。
- public landing pageにSession create APIの詳細を不要に露出しない。

## 10. Windows IPC

名前付きパイプはローカルattack surface。

- 固定magic/version。
- exact payload size。
- 720p NV12以外拒否。
- integer overflow検査。
- maximum frame length。
- ACL。
- connection client identityを可能なら検証。
- one client。
- overlapped I/O cancel。
- malformed frameでprocess crashしない。
- pipe dataをlogしない。
- Media Sourceはnetwork APIを呼ばない。
- Receiver停止時にpipeを閉じる。

PoCで固定pipe名を使う場合、同一マシンの別user/processによる競合リスクをADRへ明記する。将来はinstallation secret/user SIDを使ったnameとchallenge-responseを追加する。

## 11. Windows binaries

- Media Source DLL登録には管理者操作が必要。
- DLL配置先を一般ユーザー書込不可にする。
- productionではcode signing。
- dependency hijacking防止:
  - absolute install path
  - default DLL search pathを安全化
  - unnecessary dependency削減
- config/log directoryとbinary directoryを分離。
- app update時にDLL/CLSID整合性を確認。
- uninstallでregistry/Virtual Camera instanceをcleanup。

## 12. Logging/redaction

中央redactorが次のkey/nameを遮断:

```text
authorization
token
joinToken
receiverToken
senderToken
credential
password
sdp
iceServers.username
iceServers.credential
joinUrl
```

URLをlogする場合:

```text
https://host/send/#v=1&s=...&j=REDACTED
```

実際にはfragmentを含むURL自体をlogしない方がよい。

Crash dumpはmemory内tokenを含む可能性がある。PoCのdump共有時に注意喚起する。

## 13. Privacy notice

Sender pageに簡潔に表示:

- カメラ・マイクはWindows受信機へWebRTC送信される。
- 接続情報はCloudflare上へ最大5分保存される。
- 通常はP2P、ネットワーク条件によりTURN relayを通る。
- 映像・音声をWorker/Durable Objectへ保存しない。
- PoCは録画しない。
- Stopまたはtab closeで送信を止める。

実装が変わった場合はnoticeも更新する。

## 14. Security acceptance

- TokenがWorker/Browser/Windows logに出ない。
- Join TokenがHTTP access logへ送られない。
- expired Tokenが使えない。
- role違いTokenが使えない。
- 二重claim拒否。
- oversized request拒否。
- CSPが有効。
- pipe malformed inputでMedia Sourceが落ちない。
- DLLが一般ユーザー書込可能directoryからloadされない。
