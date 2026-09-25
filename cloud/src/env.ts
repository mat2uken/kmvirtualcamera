export interface Env {
  SESSIONS: DurableObjectNamespace;
  ASSETS?: Fetcher;
  PUBLIC_BASE_URL: string;
  SESSION_TTL_SECONDS?: string;
  SIGNALING_TIMEOUT_SECONDS?: string;
  ENABLE_TURN?: string;
  TOKEN_HMAC_SECRET?: string;
  TURN_KEY_ID?: string;
  TURN_KEY_API_TOKEN?: string;
  // Self-hosted TURN (e.g. coturn) supplied as static credentials. Takes effect
  // only when all three are set; intended for deployments that do not use the
  // Cloudflare TURN key API above.
  TURN_STATIC_URL?: string;
  TURN_STATIC_USERNAME?: string;
  TURN_STATIC_CREDENTIAL?: string;
  // Optional ICE policy override. Anything other than "relay" means "all".
  ICE_TRANSPORT_POLICY?: string;
}
