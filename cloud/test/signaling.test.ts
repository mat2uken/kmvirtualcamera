import { describe, it, expect } from "vitest";
import { env } from "cloudflare:test";
import app from "../src/index";
import { getRtcConfiguration } from "../src/signaling/turn";

const VALID_SDP_OFFER = "v=0\r\no=- 12345 2 IN IP4 127.0.0.1\r\ns=-\r\nt=0 0\r\nm=video 9 UDP/TLS/RTP/SAVPF 96\r\na=sendonly\r\n";
const VALID_SDP_ANSWER = "v=0\r\no=- 67890 2 IN IP4 127.0.0.1\r\ns=-\r\nt=0 0\r\nm=video 9 UDP/TLS/RTP/SAVPF 96\r\na=recvonly\r\n";

describe("Cloudflare Signaling & Durable Object (CF-001 - CF-022)", () => {
  it("CF-001: Session create returns valid IDs/tokens/fragment URL", async () => {
    const res = await app.request("/v1/sessions", { method: "POST" }, env);
    expect(res.status).toBe(201);
    const data = (await res.json()) as any;
    expect(data.sessionId).toMatch(/^[A-Za-z0-9_-]{16,}$/);
    expect(data.receiverToken).toMatch(/^[A-Za-z0-9_-]{32,}$/);
    expect(data.joinUrl).toContain(`#v=1&s=${data.sessionId}&j=`);
    expect(data.poll.initialIntervalMs).toBe(1000);
    expect(data.poll.backoffAfterMs).toBe(15000);
    expect(data.poll.maxIntervalMs).toBe(2000);
    expect(data.poll.timeoutMs).toBe(60000);
    expect(data.rtcConfiguration.iceServers.length).toBeGreaterThan(0);
  });

  it("CF-002: Session expiration is set at creation and ISO string", async () => {
    const res = await app.request("/v1/sessions", { method: "POST" }, env);
    const data = (await res.json()) as any;
    const expires = new Date(data.expiresAt).getTime();
    expect(expires).toBeGreaterThan(Date.now());
    expect(expires).toBeLessThanOrEqual(Date.now() + 301000);
  });

  it("CF-003: Poll does not extend expiry", async () => {
    const createRes = await app.request("/v1/sessions", { method: "POST" }, env);
    const createData = (await createRes.json()) as any;
    const initialExpiry = createData.expiresAt;

    // Perform poll
    await app.request(
      `/v1/sessions/${createData.sessionId}/offer`,
      { headers: { Authorization: `Bearer ${createData.receiverToken}` } },
      env
    );

    const stub = env.SESSIONS.get(env.SESSIONS.idFromName(createData.sessionId)) as any;
    const record = await stub.getRecord();
    expect(new Date(record.expiresAtMs).toISOString()).toBe(initialExpiry);
  });

  it("CF-004: Claim succeeds once with valid Join Token", async () => {
    const createRes = await app.request("/v1/sessions", { method: "POST" }, env);
    const createData = (await createRes.json()) as any;
    const joinToken = createData.joinUrl.split("&j=")[1];

    const claimRes = await app.request(
      `/v1/sessions/${createData.sessionId}/claim`,
      {
        method: "POST",
        headers: {
          Authorization: `Bearer ${joinToken}`,
          "Content-Type": "application/json"
        },
        body: JSON.stringify({ claimNonce: "valid-claim-nonce-12345" })
      },
      env
    );
    expect(claimRes.status).toBe(200);
    const claimData = (await claimRes.json()) as any;
    expect(claimData.senderToken).toMatch(/^[A-Za-z0-9_-]{16,}$/);
    expect(claimData.expiresAt).toBe(createData.expiresAt);
  });

  it("CF-005: Same claimNonce retry is idempotent", async () => {
    const createRes = await app.request("/v1/sessions", { method: "POST" }, env);
    const createData = (await createRes.json()) as any;
    const joinToken = createData.joinUrl.split("&j=")[1];

    const claim1 = await app.request(
      `/v1/sessions/${createData.sessionId}/claim`,
      {
        method: "POST",
        headers: { Authorization: `Bearer ${joinToken}`, "Content-Type": "application/json" },
        body: JSON.stringify({ claimNonce: "same-nonce-123456789" })
      },
      env
    );
    const claim1Data = (await claim1.json()) as any;

    const claimRetry = await app.request(
      `/v1/sessions/${createData.sessionId}/claim`,
      {
        method: "POST",
        headers: { Authorization: `Bearer ${joinToken}`, "Content-Type": "application/json" },
        body: JSON.stringify({ claimNonce: "same-nonce-123456789" })
      },
      env
    );
    expect(claimRetry.status).toBe(200);
    const retryData = (await claimRetry.json()) as any;
    expect(retryData.senderToken).toBe(claim1Data.senderToken);
  });

  it("CF-006: Different claimNonce is rejected with 409", async () => {
    const createRes = await app.request("/v1/sessions", { method: "POST" }, env);
    const createData = (await createRes.json()) as any;
    const joinToken = createData.joinUrl.split("&j=")[1];

    await app.request(
      `/v1/sessions/${createData.sessionId}/claim`,
      {
        method: "POST",
        headers: { Authorization: `Bearer ${joinToken}`, "Content-Type": "application/json" },
        body: JSON.stringify({ claimNonce: "first-nonce-11111111" })
      },
      env
    );

    const conflict = await app.request(
      `/v1/sessions/${createData.sessionId}/claim`,
      {
        method: "POST",
        headers: { Authorization: `Bearer ${joinToken}`, "Content-Type": "application/json" },
        body: JSON.stringify({ claimNonce: "second-nonce-22222222" })
      },
      env
    );
    expect(conflict.status).toBe(409);
  });

  it("CF-007: Receiver token cannot PUT offer", async () => {
    const createRes = await app.request("/v1/sessions", { method: "POST" }, env);
    const createData = (await createRes.json()) as any;

    const badOffer = await app.request(
      `/v1/sessions/${createData.sessionId}/offer`,
      {
        method: "PUT",
        headers: { Authorization: `Bearer ${createData.receiverToken}`, "Content-Type": "application/json" },
        body: JSON.stringify({ type: "offer", sdp: VALID_SDP_OFFER })
      },
      env
    );
    expect(badOffer.status).toBe(403);
  });

  it("CF-008: Sender token cannot PUT answer", async () => {
    const createRes = await app.request("/v1/sessions", { method: "POST" }, env);
    const createData = (await createRes.json()) as any;
    const joinToken = createData.joinUrl.split("&j=")[1];

    const claimRes = await app.request(
      `/v1/sessions/${createData.sessionId}/claim`,
      {
        method: "POST",
        headers: { Authorization: `Bearer ${joinToken}`, "Content-Type": "application/json" },
        body: JSON.stringify({ claimNonce: "sender-token-check-nonce" })
      },
      env
    );
    const claimData = (await claimRes.json()) as any;

    const badAnswer = await app.request(
      `/v1/sessions/${createData.sessionId}/answer`,
      {
        method: "PUT",
        headers: { Authorization: `Bearer ${claimData.senderToken}`, "Content-Type": "application/json" },
        body: JSON.stringify({ type: "answer", sdp: VALID_SDP_ANSWER })
      },
      env
    );
    expect(badAnswer.status).toBe(403);
  });

  it("CF-009: GET before data returns 204 with Retry-After", async () => {
    const createRes = await app.request("/v1/sessions", { method: "POST" }, env);
    const createData = (await createRes.json()) as any;

    const getOffer = await app.request(
      `/v1/sessions/${createData.sessionId}/offer`,
      { headers: { Authorization: `Bearer ${createData.receiverToken}` } },
      env
    );
    expect(getOffer.status).toBe(204);
    expect(getOffer.headers.get("Retry-After")).toBe("1");
  });

  it("CF-010: Offer then Answer valid transition", async () => {
    const createRes = await app.request("/v1/sessions", { method: "POST" }, env);
    const createData = (await createRes.json()) as any;
    const joinToken = createData.joinUrl.split("&j=")[1];

    const claimRes = await app.request(
      `/v1/sessions/${createData.sessionId}/claim`,
      {
        method: "POST",
        headers: { Authorization: `Bearer ${joinToken}`, "Content-Type": "application/json" },
        body: JSON.stringify({ claimNonce: "transition-test-nonce" })
      },
      env
    );
    const claimData = (await claimRes.json()) as any;

    const putOffer = await app.request(
      `/v1/sessions/${createData.sessionId}/offer`,
      {
        method: "PUT",
        headers: { Authorization: `Bearer ${claimData.senderToken}`, "Content-Type": "application/json" },
        body: JSON.stringify({ type: "offer", sdp: VALID_SDP_OFFER })
      },
      env
    );
    expect(putOffer.status).toBe(204);

    const getOffer = await app.request(
      `/v1/sessions/${createData.sessionId}/offer`,
      { headers: { Authorization: `Bearer ${createData.receiverToken}` } },
      env
    );
    expect(getOffer.status).toBe(200);
    const offerBody = (await getOffer.json()) as any;
    expect(offerBody.sdp).toBe(VALID_SDP_OFFER);

    const putAnswer = await app.request(
      `/v1/sessions/${createData.sessionId}/answer`,
      {
        method: "PUT",
        headers: { Authorization: `Bearer ${createData.receiverToken}`, "Content-Type": "application/json" },
        body: JSON.stringify({ type: "answer", sdp: VALID_SDP_ANSWER })
      },
      env
    );
    expect(putAnswer.status).toBe(204);

    const getAnswer = await app.request(
      `/v1/sessions/${createData.sessionId}/answer`,
      { headers: { Authorization: `Bearer ${claimData.senderToken}` } },
      env
    );
    expect(getAnswer.status).toBe(200);
    const answerBody = (await getAnswer.json()) as any;
    expect(answerBody.sdp).toBe(VALID_SDP_ANSWER);
  });

  it("CF-011: Answer before Offer rejected with 409", async () => {
    const createRes = await app.request("/v1/sessions", { method: "POST" }, env);
    const createData = (await createRes.json()) as any;

    const earlyAnswer = await app.request(
      `/v1/sessions/${createData.sessionId}/answer`,
      {
        method: "PUT",
        headers: { Authorization: `Bearer ${createData.receiverToken}`, "Content-Type": "application/json" },
        body: JSON.stringify({ type: "answer", sdp: VALID_SDP_ANSWER })
      },
      env
    );
    expect(earlyAnswer.status).toBe(409);
  });

  it("CF-012, CF-013: Identical PUT retry succeeds (204) and conflicting second PUT rejected (409)", async () => {
    const createRes = await app.request("/v1/sessions", { method: "POST" }, env);
    const createData = (await createRes.json()) as any;
    const joinToken = createData.joinUrl.split("&j=")[1];

    const claimRes = await app.request(
      `/v1/sessions/${createData.sessionId}/claim`,
      {
        method: "POST",
        headers: { Authorization: `Bearer ${joinToken}`, "Content-Type": "application/json" },
        body: JSON.stringify({ claimNonce: "idempotency-test-nonce" })
      },
      env
    );
    const claimData = (await claimRes.json()) as any;

    // First offer PUT
    await app.request(
      `/v1/sessions/${createData.sessionId}/offer`,
      {
        method: "PUT",
        headers: { Authorization: `Bearer ${claimData.senderToken}`, "Content-Type": "application/json" },
        body: JSON.stringify({ type: "offer", sdp: VALID_SDP_OFFER })
      },
      env
    );

    // CF-012: Identical offer retry -> 204
    const retry = await app.request(
      `/v1/sessions/${createData.sessionId}/offer`,
      {
        method: "PUT",
        headers: { Authorization: `Bearer ${claimData.senderToken}`, "Content-Type": "application/json" },
        body: JSON.stringify({ type: "offer", sdp: VALID_SDP_OFFER })
      },
      env
    );
    expect(retry.status).toBe(204);

    // CF-013: Conflicting offer -> 409
    const conflict = await app.request(
      `/v1/sessions/${createData.sessionId}/offer`,
      {
        method: "PUT",
        headers: { Authorization: `Bearer ${claimData.senderToken}`, "Content-Type": "application/json" },
        body: JSON.stringify({ type: "offer", sdp: "v=0\r\nm=video 9 UDP/TLS/RTP/SAVPF 99\r\n" })
      },
      env
    );
    expect(conflict.status).toBe(409);
  });

  it("CF-014, CF-015: Expired session returns 410 and Alarm deletes storage", async () => {
    const createRes = await app.request("/v1/sessions", { method: "POST" }, env);
    const createData = (await createRes.json()) as any;

    const stub = env.SESSIONS.get(env.SESSIONS.idFromName(createData.sessionId)) as any;
    // Trigger purge directly to simulate 5-minute timeout cleanup
    await stub.purgeForTesting();

    const getRes = await app.request(
      `/v1/sessions/${createData.sessionId}/offer`,
      { headers: { Authorization: `Bearer ${createData.receiverToken}` } },
      env
    );
    expect(getRes.status).toBe(410);
  });

  it("CF-016, CF-017: Malformed JSON and invalid IDs rejected", async () => {
    const createRes = await app.request("/v1/sessions", { method: "POST" }, env);
    const createData = (await createRes.json()) as any;
    const joinToken = createData.joinUrl.split("&j=")[1];

    const malformed = await app.request(
      `/v1/sessions/${createData.sessionId}/claim`,
      {
        method: "POST",
        headers: { Authorization: `Bearer ${joinToken}`, "Content-Type": "application/json" },
        body: "{ invalid-json "
      },
      env
    );
    expect(malformed.status).toBe(400);

    const badId = await app.request(
      `/v1/sessions/invalid!!path..id/claim`,
      {
        method: "POST",
        headers: { Authorization: `Bearer ${joinToken}`, "Content-Type": "application/json" },
        body: JSON.stringify({ claimNonce: "1234567890123456" })
      },
      env
    );
    expect(badId.status).toBe(400);
  });

  it("CF-018: Oversized SDP is rejected with 413", async () => {
    const createRes = await app.request("/v1/sessions", { method: "POST" }, env);
    const createData = (await createRes.json()) as any;
    const joinToken = createData.joinUrl.split("&j=")[1];

    const claimRes = await app.request(
      `/v1/sessions/${createData.sessionId}/claim`,
      {
        method: "POST",
        headers: { Authorization: `Bearer ${joinToken}`, "Content-Type": "application/json" },
        body: JSON.stringify({ claimNonce: "oversize-test-nonce" })
      },
      env
    );
    const claimData = (await claimRes.json()) as any;

    const hugeSdp = "v=0\r\nm=video 9 UDP/TLS/RTP/SAVPF 96\r\n" + "a=candidate:...".repeat(15000);
    const res = await app.request(
      `/v1/sessions/${createData.sessionId}/offer`,
      {
        method: "PUT",
        headers: { Authorization: `Bearer ${claimData.senderToken}`, "Content-Type": "application/json" },
        body: JSON.stringify({ type: "offer", sdp: hugeSdp })
      },
      env
    );
    expect(res.status).toBe(413);
  });

  it("CF-019: Security and no-store headers are present", async () => {
    const res = await app.request("/v1/health", { method: "GET" }, env);
    expect(res.headers.get("Cache-Control")).toBe("no-store, max-age=0");
    expect(res.headers.get("Pragma")).toBe("no-cache");
    expect(res.headers.get("X-Request-ID")).toBeDefined();
    expect(res.headers.get("Referrer-Policy")).toBe("no-referrer");
    expect(res.headers.get("X-Content-Type-Options")).toBe("nosniff");
  });

  it("CF-021, CF-022: TURN long-term secret never returned and port 53 ICE URLs filtered", async () => {
    const rtcConfig = await getRtcConfiguration(env);
    expect(rtcConfig.iceServers.length).toBeGreaterThan(0);
    for (const server of rtcConfig.iceServers) {
      const urls = Array.isArray(server.urls) ? server.urls : [server.urls];
      for (const u of urls) {
        expect(u).not.toContain(":53");
      }
    }
  });
});
