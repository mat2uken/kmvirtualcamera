export interface PollPolicy {
  initialIntervalMs: number;
  backoffAfterMs: number;
  maxIntervalMs: number;
  timeoutMs: number;
}

export interface ClaimResult {
  senderToken: string;
  expiresAt: string;
  poll: PollPolicy;
  rtcConfiguration: RTCConfiguration;
}

export class SignalingClient {
  private baseUrl = "";

  constructor(baseUrl = "") {
    this.baseUrl = baseUrl.replace(/\/+$/, "");
  }

  async claim(
    sessionId: string,
    joinToken: string,
    claimNonce: string,
    signal?: AbortSignal
  ): Promise<ClaimResult> {
    const res = await fetch(`${this.baseUrl}/v1/sessions/${encodeURIComponent(sessionId)}/claim`, {
      method: "POST",
      headers: {
        Authorization: `Bearer ${joinToken}`,
        "Content-Type": "application/json"
      },
      body: JSON.stringify({ claimNonce, client: { name: "vanjs-sender", version: "0.1.0" } }),
      signal
    });

    if (!res.ok) {
      const err = await res.json().catch(() => ({ error: { message: `Claim failed (${res.status})` } }));
      throw new Error(err.error?.message || `Claim HTTP error ${res.status}`);
    }

    return await res.json();
  }

  async putOffer(
    sessionId: string,
    senderToken: string,
    sdp: string,
    signal?: AbortSignal
  ): Promise<void> {
    const res = await fetch(`${this.baseUrl}/v1/sessions/${encodeURIComponent(sessionId)}/offer`, {
      method: "PUT",
      headers: {
        Authorization: `Bearer ${senderToken}`,
        "Content-Type": "application/json"
      },
      body: JSON.stringify({ type: "offer", sdp }),
      signal
    });

    if (!res.ok) {
      const err = await res.json().catch(() => ({ error: { message: `Offer upload failed (${res.status})` } }));
      throw new Error(err.error?.message || `Offer HTTP error ${res.status}`);
    }
  }

  async pollAnswer(
    sessionId: string,
    senderToken: string,
    pollPolicy: PollPolicy,
    signal?: AbortSignal
  ): Promise<string> {
    const startTime = Date.now();
    let interval = pollPolicy.initialIntervalMs || 1000;

    while (Date.now() - startTime < (pollPolicy.timeoutMs || 60000)) {
      if (signal?.aborted) {
        throw new DOMException("Polling aborted", "AbortError");
      }

      const res = await fetch(`${this.baseUrl}/v1/sessions/${encodeURIComponent(sessionId)}/answer`, {
        method: "GET",
        headers: {
          Authorization: `Bearer ${senderToken}`
        },
        signal
      });

      if (res.status === 200) {
        const data = (await res.json()) as { sdp: string };
        return data.sdp;
      }

      if (res.status === 204) {
        if (Date.now() - startTime > (pollPolicy.backoffAfterMs || 15000)) {
          interval = Math.min(interval + 500, pollPolicy.maxIntervalMs || 2000);
        }
        await new Promise((r) => setTimeout(r, interval));
        continue;
      }

      const err = await res.json().catch(() => ({ error: { message: `Answer polling error (${res.status})` } }));
      throw new Error(err.error?.message || `Answer polling HTTP ${res.status}`);
    }

    throw new Error("Signaling timeout: Windows receiver did not answer within 60s.");
  }

  async deleteSession(sessionId: string, token: string): Promise<void> {
    try {
      await fetch(`${this.baseUrl}/v1/sessions/${encodeURIComponent(sessionId)}`, {
        method: "DELETE",
        headers: {
          Authorization: `Bearer ${token}`
        }
      });
    } catch {
      // Best-effort cleanup
    }
  }
}
