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
}
