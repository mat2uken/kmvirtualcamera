import { DurableObject } from "cloudflare:workers";
import { Env } from "../env";
import { SessionRecord, SessionState } from "../signaling/types";
import { hashToken, timingSafeEqual } from "../signaling/tokens";

export class SessionDurableObject extends DurableObject {
  constructor(ctx: DurableObjectState, env: Env) {
    super(ctx, env);
  }

  async getRecord(): Promise<SessionRecord | undefined> {
    return await this.ctx.storage.get<SessionRecord>("session");
  }

  async purgeForTesting(): Promise<void> {
    await this.ctx.storage.deleteAll();
  }

  async createSession(record: SessionRecord): Promise<{ success: boolean; error?: string }> {
    const existing = await this.ctx.storage.get<SessionRecord>("session");
    if (existing) {
      return { success: false, error: "SESSION_EXISTS" };
    }

    await this.ctx.storage.put("session", record);
    await this.ctx.storage.setAlarm(record.expiresAtMs);
    return { success: true };
  }

  async claimSession(
    joinToken: string,
    claimNonce: string,
    senderToken: string
  ): Promise<{ status: number; code?: string; message?: string; record?: SessionRecord }> {
    const record = await this.ctx.storage.get<SessionRecord>("session");
    if (!record || Date.now() >= record.expiresAtMs) {
      return { status: 410, code: "SESSION_EXPIRED", message: "Signaling session has expired or does not exist." };
    }

    const joinHash = await hashToken(joinToken);
    if (!timingSafeEqual(joinHash, record.joinTokenHash)) {
      return { status: 403, code: "INVALID_TOKEN", message: "Invalid join token." };
    }

    const nonceHash = await hashToken(claimNonce);

    if (record.state !== "CREATED") {
      if (record.claimNonceHash && timingSafeEqual(nonceHash, record.claimNonceHash)) {
        return { status: 200, record };
      }
      return { status: 409, code: "SESSION_ALREADY_CLAIMED", message: "Session has already been claimed by another sender." };
    }

    record.state = "CLAIMED";
    record.claimNonceHash = nonceHash;
    record.senderTokenHash = await hashToken(senderToken);
    await this.ctx.storage.put("session", record);

    return { status: 200, record };
  }

  async putOffer(
    senderToken: string,
    sdp: string
  ): Promise<{ status: number; code?: string; message?: string }> {
    const record = await this.ctx.storage.get<SessionRecord>("session");
    if (!record || Date.now() >= record.expiresAtMs) {
      return { status: 410, code: "SESSION_EXPIRED", message: "Signaling session has expired." };
    }

    if (!record.senderTokenHash) {
      return { status: 403, code: "INVALID_STATE", message: "Session has not been claimed yet." };
    }

    const senderHash = await hashToken(senderToken);
    if (!timingSafeEqual(senderHash, record.senderTokenHash)) {
      return { status: 403, code: "FORBIDDEN", message: "Invalid sender token." };
    }

    if (record.offer) {
      if (record.offer.sdp === sdp) {
        return { status: 204 };
      }
      return { status: 409, code: "INVALID_STATE", message: "Conflicting offer has already been submitted." };
    }

    if (record.state !== "CLAIMED") {
      return { status: 409, code: "INVALID_STATE", message: `Cannot submit offer in state ${record.state}.` };
    }

    record.offer = {
      type: "offer",
      sdp,
      storedAtMs: Date.now()
    };
    record.state = "OFFER_READY";
    await this.ctx.storage.put("session", record);

    return { status: 204 };
  }

  async getOffer(
    receiverToken: string
  ): Promise<{ status: number; code?: string; message?: string; offer?: { type: "offer"; sdp: string } }> {
    const record = await this.ctx.storage.get<SessionRecord>("session");
    if (!record || Date.now() >= record.expiresAtMs) {
      return { status: 410, code: "SESSION_EXPIRED", message: "Signaling session has expired." };
    }

    const receiverHash = await hashToken(receiverToken);
    if (!timingSafeEqual(receiverHash, record.receiverTokenHash)) {
      return { status: 403, code: "FORBIDDEN", message: "Invalid receiver token." };
    }

    if (!record.offer) {
      return { status: 204 };
    }

    return { status: 200, offer: { type: "offer", sdp: record.offer.sdp } };
  }

  async putAnswer(
    receiverToken: string,
    sdp: string
  ): Promise<{ status: number; code?: string; message?: string }> {
    const record = await this.ctx.storage.get<SessionRecord>("session");
    if (!record || Date.now() >= record.expiresAtMs) {
      return { status: 410, code: "SESSION_EXPIRED", message: "Signaling session has expired." };
    }

    const receiverHash = await hashToken(receiverToken);
    if (!timingSafeEqual(receiverHash, record.receiverTokenHash)) {
      return { status: 403, code: "FORBIDDEN", message: "Invalid receiver token." };
    }

    if (record.answer) {
      if (record.answer.sdp === sdp) {
        return { status: 204 };
      }
      return { status: 409, code: "INVALID_STATE", message: "Conflicting answer has already been submitted." };
    }

    if (record.state !== "OFFER_READY") {
      return { status: 409, code: "INVALID_STATE", message: `Cannot submit answer before offer is ready (current state: ${record.state}).` };
    }

    record.answer = {
      type: "answer",
      sdp,
      storedAtMs: Date.now()
    };
    record.state = "ANSWER_READY";
    await this.ctx.storage.put("session", record);

    return { status: 204 };
  }

  async getAnswer(
    senderToken: string
  ): Promise<{ status: number; code?: string; message?: string; answer?: { type: "answer"; sdp: string } }> {
    const record = await this.ctx.storage.get<SessionRecord>("session");
    if (!record || Date.now() >= record.expiresAtMs) {
      return { status: 410, code: "SESSION_EXPIRED", message: "Signaling session has expired." };
    }

    if (!record.senderTokenHash) {
      return { status: 403, code: "INVALID_STATE", message: "Session not claimed." };
    }

    const senderHash = await hashToken(senderToken);
    if (!timingSafeEqual(senderHash, record.senderTokenHash)) {
      return { status: 403, code: "FORBIDDEN", message: "Invalid sender token." };
    }

    if (!record.answer) {
      return { status: 204 };
    }

    return { status: 200, answer: { type: "answer", sdp: record.answer.sdp } };
  }

  async deleteSession(
    token: string
  ): Promise<{ status: number; code?: string; message?: string }> {
    const record = await this.ctx.storage.get<SessionRecord>("session");
    if (!record) {
      return { status: 204 };
    }

    const tokenHash = await hashToken(token);
    const isReceiver = timingSafeEqual(tokenHash, record.receiverTokenHash);
    const isSender = record.senderTokenHash && timingSafeEqual(tokenHash, record.senderTokenHash);

    if (!isReceiver && !isSender) {
      return { status: 403, code: "FORBIDDEN", message: "Invalid authorization token for delete." };
    }

    await this.ctx.storage.deleteAll();
    return { status: 204 };
  }

  async alarm(): Promise<void> {
    await this.ctx.storage.deleteAll();
  }
}
