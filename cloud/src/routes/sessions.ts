import { Hono } from "hono";
import { Env } from "../env";
import {
  generateSessionId,
  generateRandomToken,
  hashToken,
  deriveSenderToken
} from "../signaling/tokens";
import {
  isValidSessionId,
  isValidNonce,
  extractBearerToken,
  isValidSdp
} from "../signaling/validation";
import { getRtcConfiguration } from "../signaling/turn";
import {
  CreateSessionRequest,
  CreateSessionResponse,
  ClaimSessionRequest,
  ClaimSessionResponse,
  OfferDescription,
  AnswerDescription,
  SessionRecord
} from "../signaling/types";
import { SessionDurableObject } from "../durable/session-object";

const sessions = new Hono<{ Bindings: Env; Variables: { requestId: string } }>();

// POST /v1/sessions - Create a short-lived signaling session
sessions.post("/", async (c) => {
  const requestId = c.get("requestId");

  let body: CreateSessionRequest = {};
  if (c.req.header("content-type")?.includes("application/json")) {
    try {
      body = await c.req.json();
    } catch {
      return c.json(
        { error: { code: "INVALID_JSON", message: "Malformed JSON payload.", retryable: false, requestId } },
        400
      );
    }
  }

  const sessionId = generateSessionId();
  const receiverToken = generateRandomToken(32);
  const joinToken = generateRandomToken(32);

  const ttlSeconds = parseInt(c.env.SESSION_TTL_SECONDS || "300", 10);
  const timeoutSeconds = parseInt(c.env.SIGNALING_TIMEOUT_SECONDS || "60", 10);
  const now = Date.now();
  const expiresAtMs = now + ttlSeconds * 1000;
  const expiresAt = new Date(expiresAtMs).toISOString();

  let rtcConfiguration;
  try {
    rtcConfiguration = await getRtcConfiguration(c.env);
  } catch (err) {
    return c.json(
      { error: { code: "DEPENDENCY_FAILED", message: "TURN credential generation failed.", retryable: true, requestId } },
      503
    );
  }

  const record: SessionRecord = {
    schemaVersion: 1,
    sessionId,
    state: "CREATED",
    createdAtMs: now,
    expiresAtMs,
    receiverTokenHash: await hashToken(receiverToken),
    joinTokenHash: await hashToken(joinToken),
    rtcConfiguration
  };

  const id = c.env.SESSIONS.idFromName(sessionId);
  const stub = c.env.SESSIONS.get(id) as unknown as SessionDurableObject;
  const result = await stub.createSession(record);

  if (!result.success) {
    return c.json(
      { error: { code: "SESSION_CONFLICT", message: "Session collision occurred.", retryable: true, requestId } },
      409
    );
  }

  let baseUrl = c.env.PUBLIC_BASE_URL?.trim();
  if (!baseUrl || baseUrl.includes("127.0.0.1") || baseUrl.includes("localhost")) {
    try {
      baseUrl = new URL(c.req.url).origin;
    } catch {
      baseUrl = baseUrl || "http://127.0.0.1:8787";
    }
  }
  baseUrl = baseUrl.replace(/\/+$/, "");
  const joinUrl = `${baseUrl}/send/#v=1&s=${sessionId}&j=${joinToken}`;

  const responsePayload: CreateSessionResponse = {
    sessionId,
    receiverToken,
    joinUrl,
    expiresAt,
    poll: {
      initialIntervalMs: 1000,
      backoffAfterMs: 15000,
      maxIntervalMs: 2000,
      timeoutMs: timeoutSeconds * 1000
    },
    rtcConfiguration
  };

  return c.json(responsePayload, 201);
});

// POST /v1/sessions/:sessionId/claim - Claim a session once as the browser Sender
sessions.post("/:sessionId/claim", async (c) => {
  const requestId = c.get("requestId");
  const sessionId = c.req.param("sessionId");

  if (!isValidSessionId(sessionId)) {
    return c.json(
      { error: { code: "INVALID_SESSION_ID", message: "Invalid session ID format.", retryable: false, requestId } },
      400
    );
  }

  const authHeader = c.req.header("authorization");
  const joinToken = extractBearerToken(authHeader);
  if (!joinToken) {
    return c.json(
      { error: { code: "UNAUTHORIZED", message: "Missing or invalid Bearer Join Token.", retryable: false, requestId } },
      401
    );
  }

  let body: ClaimSessionRequest;
  try {
    body = await c.req.json();
  } catch {
    return c.json(
      { error: { code: "INVALID_JSON", message: "Malformed JSON payload.", retryable: false, requestId } },
      400
    );
  }

  if (!body || !isValidNonce(body.claimNonce)) {
    return c.json(
      { error: { code: "INVALID_CLAIM_NONCE", message: "Missing or invalid claimNonce.", retryable: false, requestId } },
      400
    );
  }

  const secret = c.env.TOKEN_HMAC_SECRET || "default-dev-hmac-secret-kmvirtualcamera-2026";
  const senderToken = await deriveSenderToken(secret, sessionId, body.claimNonce);

  const id = c.env.SESSIONS.idFromName(sessionId);
  const stub = c.env.SESSIONS.get(id) as unknown as SessionDurableObject;
  const res = await stub.claimSession(joinToken, body.claimNonce, senderToken);

  if (res.status !== 200 || !res.record) {
    return c.json(
      { error: { code: res.code || "CLAIM_FAILED", message: res.message || "Claim failed", retryable: false, requestId } },
      res.status as 400 | 401 | 403 | 409 | 410
    );
  }

  const timeoutSeconds = parseInt(c.env.SIGNALING_TIMEOUT_SECONDS || "60", 10);
  const responsePayload: ClaimSessionResponse = {
    senderToken,
    expiresAt: new Date(res.record.expiresAtMs).toISOString(),
    poll: {
      initialIntervalMs: 1000,
      backoffAfterMs: 15000,
      maxIntervalMs: 2000,
      timeoutMs: timeoutSeconds * 1000
    },
    rtcConfiguration: res.record.rtcConfiguration
  };

  return c.json(responsePayload, 200);
});

// PUT /v1/sessions/:sessionId/offer - Store the browser Sender's SDP offer
sessions.put("/:sessionId/offer", async (c) => {
  const requestId = c.get("requestId");
  const sessionId = c.req.param("sessionId");

  if (!isValidSessionId(sessionId)) {
    return c.json(
      { error: { code: "INVALID_SESSION_ID", message: "Invalid session ID format.", retryable: false, requestId } },
      400
    );
  }

  const authHeader = c.req.header("authorization");
  const senderToken = extractBearerToken(authHeader);
  if (!senderToken) {
    return c.json(
      { error: { code: "UNAUTHORIZED", message: "Missing or invalid Bearer Sender Token.", retryable: false, requestId } },
      401
    );
  }

  let body: OfferDescription;
  try {
    body = await c.req.json();
  } catch {
    return c.json(
      { error: { code: "INVALID_JSON", message: "Malformed JSON payload.", retryable: false, requestId } },
      400
    );
  }

  if (body?.type !== "offer") {
    return c.json(
      { error: { code: "INVALID_SDP_TYPE", message: "type must be 'offer'.", retryable: false, requestId } },
      400
    );
  }

  const sdpCheck = isValidSdp(body.sdp, "offer");
  if (!sdpCheck.valid) {
    return c.json(
      { error: { code: "INVALID_SDP", message: sdpCheck.error || "Invalid SDP.", retryable: false, requestId } },
      sdpCheck.error?.includes("exceeds") ? 413 : 400
    );
  }

  const id = c.env.SESSIONS.idFromName(sessionId);
  const stub = c.env.SESSIONS.get(id) as unknown as SessionDurableObject;
  const res = await stub.putOffer(senderToken, body.sdp);

  if (res.status === 204) {
    return c.body(null, 204);
  }

  return c.json(
    { error: { code: res.code || "OFFER_FAILED", message: res.message || "Failed to store offer.", retryable: false, requestId } },
    res.status as 400 | 401 | 403 | 409 | 410
  );
});

// GET /v1/sessions/:sessionId/offer - Poll for the Sender's SDP offer
sessions.get("/:sessionId/offer", async (c) => {
  const requestId = c.get("requestId");
  const sessionId = c.req.param("sessionId");

  if (!isValidSessionId(sessionId)) {
    return c.json(
      { error: { code: "INVALID_SESSION_ID", message: "Invalid session ID format.", retryable: false, requestId } },
      400
    );
  }

  const authHeader = c.req.header("authorization");
  const receiverToken = extractBearerToken(authHeader);
  if (!receiverToken) {
    return c.json(
      { error: { code: "UNAUTHORIZED", message: "Missing or invalid Bearer Receiver Token.", retryable: false, requestId } },
      401
    );
  }

  const id = c.env.SESSIONS.idFromName(sessionId);
  const stub = c.env.SESSIONS.get(id) as unknown as SessionDurableObject;
  const res = await stub.getOffer(receiverToken);

  if (res.status === 204) {
    c.header("Retry-After", "1");
    return c.body(null, 204);
  }

  if (res.status === 200 && res.offer) {
    return c.json(res.offer, 200);
  }

  return c.json(
    { error: { code: res.code || "GET_OFFER_FAILED", message: res.message || "Failed to get offer.", retryable: false, requestId } },
    res.status as 400 | 401 | 403 | 409 | 410
  );
});

// PUT /v1/sessions/:sessionId/answer - Store the Windows Receiver's SDP answer
sessions.put("/:sessionId/answer", async (c) => {
  const requestId = c.get("requestId");
  const sessionId = c.req.param("sessionId");

  if (!isValidSessionId(sessionId)) {
    return c.json(
      { error: { code: "INVALID_SESSION_ID", message: "Invalid session ID format.", retryable: false, requestId } },
      400
    );
  }

  const authHeader = c.req.header("authorization");
  const receiverToken = extractBearerToken(authHeader);
  if (!receiverToken) {
    return c.json(
      { error: { code: "UNAUTHORIZED", message: "Missing or invalid Bearer Receiver Token.", retryable: false, requestId } },
      401
    );
  }

  let body: AnswerDescription;
  try {
    body = await c.req.json();
  } catch {
    return c.json(
      { error: { code: "INVALID_JSON", message: "Malformed JSON payload.", retryable: false, requestId } },
      400
    );
  }

  if (body?.type !== "answer") {
    return c.json(
      { error: { code: "INVALID_SDP_TYPE", message: "type must be 'answer'.", retryable: false, requestId } },
      400
    );
  }

  const sdpCheck = isValidSdp(body.sdp, "answer");
  if (!sdpCheck.valid) {
    return c.json(
      { error: { code: "INVALID_SDP", message: sdpCheck.error || "Invalid SDP.", retryable: false, requestId } },
      sdpCheck.error?.includes("exceeds") ? 413 : 400
    );
  }

  const id = c.env.SESSIONS.idFromName(sessionId);
  const stub = c.env.SESSIONS.get(id) as unknown as SessionDurableObject;
  const res = await stub.putAnswer(receiverToken, body.sdp);

  if (res.status === 204) {
    return c.body(null, 204);
  }

  return c.json(
    { error: { code: res.code || "ANSWER_FAILED", message: res.message || "Failed to store answer.", retryable: false, requestId } },
    res.status as 400 | 401 | 403 | 409 | 410
  );
});

// GET /v1/sessions/:sessionId/answer - Poll for the Receiver's SDP answer
sessions.get("/:sessionId/answer", async (c) => {
  const requestId = c.get("requestId");
  const sessionId = c.req.param("sessionId");

  if (!isValidSessionId(sessionId)) {
    return c.json(
      { error: { code: "INVALID_SESSION_ID", message: "Invalid session ID format.", retryable: false, requestId } },
      400
    );
  }

  const authHeader = c.req.header("authorization");
  const senderToken = extractBearerToken(authHeader);
  if (!senderToken) {
    return c.json(
      { error: { code: "UNAUTHORIZED", message: "Missing or invalid Bearer Sender Token.", retryable: false, requestId } },
      401
    );
  }

  const id = c.env.SESSIONS.idFromName(sessionId);
  const stub = c.env.SESSIONS.get(id) as unknown as SessionDurableObject;
  const res = await stub.getAnswer(senderToken);

  if (res.status === 204) {
    c.header("Retry-After", "1");
    return c.body(null, 204);
  }

  if (res.status === 200 && res.answer) {
    return c.json(res.answer, 200);
  }

  return c.json(
    { error: { code: res.code || "GET_ANSWER_FAILED", message: res.message || "Failed to get answer.", retryable: false, requestId } },
    res.status as 400 | 401 | 403 | 409 | 410
  );
});

// DELETE /v1/sessions/:sessionId - Explicit session termination
sessions.delete("/:sessionId", async (c) => {
  const requestId = c.get("requestId");
  const sessionId = c.req.param("sessionId");

  if (!isValidSessionId(sessionId)) {
    return c.json(
      { error: { code: "INVALID_SESSION_ID", message: "Invalid session ID format.", retryable: false, requestId } },
      400
    );
  }

  const authHeader = c.req.header("authorization");
  const token = extractBearerToken(authHeader);
  if (!token) {
    return c.json(
      { error: { code: "UNAUTHORIZED", message: "Missing or invalid Bearer Token.", retryable: false, requestId } },
      401
    );
  }

  const id = c.env.SESSIONS.idFromName(sessionId);
  const stub = c.env.SESSIONS.get(id) as unknown as SessionDurableObject;
  const res = await stub.deleteSession(token);

  if (res.status === 204) {
    return c.body(null, 204);
  }

  return c.json(
    { error: { code: res.code || "DELETE_FAILED", message: res.message || "Failed to delete session.", retryable: false, requestId } },
    res.status as 400 | 401 | 403 | 409 | 410
  );
});

export { sessions };
