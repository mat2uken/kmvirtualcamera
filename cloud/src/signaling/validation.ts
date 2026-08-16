const BASE64URL_REGEX = /^[A-Za-z0-9_-]{16,64}$/;
const NONCE_REGEX = /^[A-Za-z0-9_-]{16,128}$/;

export function isValidSessionId(id: string): boolean {
  return typeof id === "string" && BASE64URL_REGEX.test(id);
}

export function isValidNonce(nonce: string): boolean {
  return typeof nonce === "string" && NONCE_REGEX.test(nonce);
}

export function extractBearerToken(authHeader: string | null | undefined): string | null {
  if (!authHeader) return null;
  const match = authHeader.match(/^Bearer\s+([A-Za-z0-9_-]{16,256})$/);
  return match ? match[1] : null;
}

export function isValidSdp(sdp: unknown, type: "offer" | "answer"): { valid: boolean; error?: string } {
  if (typeof sdp !== "string") {
    return { valid: false, error: "SDP must be a string." };
  }
  if (sdp.length > 131072) {
    return { valid: false, error: "SDP exceeds maximum size of 131072 bytes." };
  }
  if (sdp.includes("\0")) {
    return { valid: false, error: "SDP contains null bytes." };
  }
  const trimmed = sdp.trimStart();
  if (!trimmed.startsWith("v=0")) {
    return { valid: false, error: "SDP must start with 'v=0'." };
  }
  if (!sdp.includes("m=video") && !sdp.includes("m=audio")) {
    return { valid: false, error: "SDP must contain at least one media section (m=audio or m=video)." };
  }
  return { valid: true };
}
