# Security Policy

## 1. Core Principles
- **No Token/SDP Logging**: Bearer Tokens, Join Tokens in URL fragments, raw SDP payloads, and TURN credentials are never output to application logs, standard out, or remote observability endpoints.
- **Short-Lived Ephemeral State**: Signaling state in Cloudflare Durable Objects is stored in memory/ephemeral SQLite with a strict 300s TTL. Alarms unconditionally purge session records.
- **URL Fragment Security**: Join Tokens are delivered via URL hash fragments (`#v=1&s=...&j=...`) which are never transmitted in HTTP request lines and are purged from browser history via `history.replaceState()`.
- **Role Separation**: Receiver Tokens, Join Tokens, and Sender Tokens are cryptographically distinct. Senders cannot fetch other offers or modify receiver answers.
- **Process Isolation**: The Media Foundation Virtual Camera DLL runs in the Windows Frame Server security context and communicates with Receiver.exe solely through bounded, verified Named Pipe IPC.
