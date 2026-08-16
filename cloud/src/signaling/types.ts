export type SessionState =
  | "CREATED"
  | "CLAIMED"
  | "OFFER_READY"
  | "ANSWER_READY"
  | "CLOSED";

export interface IceServerDto {
  urls: string | string[];
  username?: string;
  credential?: string;
}

export interface RtcConfigurationDto {
  iceServers: IceServerDto[];
  iceTransportPolicy?: "all" | "relay";
}

export interface PollPolicyDto {
  initialIntervalMs: number;
  backoffAfterMs: number;
  maxIntervalMs: number;
  timeoutMs: number;
}

export interface ClientInfoDto {
  name?: string;
  version?: string;
}

export interface CreateSessionRequest {
  client?: ClientInfoDto;
}

export interface CreateSessionResponse {
  sessionId: string;
  receiverToken: string;
  joinUrl: string;
  expiresAt: string;
  poll: PollPolicyDto;
  rtcConfiguration: RtcConfigurationDto;
}

export interface ClaimSessionRequest {
  claimNonce: string;
  client?: ClientInfoDto;
}

export interface ClaimSessionResponse {
  senderToken: string;
  expiresAt: string;
  poll: PollPolicyDto;
  rtcConfiguration: RtcConfigurationDto;
}

export interface OfferDescription {
  type: "offer";
  sdp: string;
}

export interface AnswerDescription {
  type: "answer";
  sdp: string;
}

export interface ErrorDetail {
  code: string;
  message: string;
  retryable: boolean;
  requestId?: string;
}

export interface ErrorResponse {
  error: ErrorDetail;
}

export interface SessionRecord {
  schemaVersion: 1;
  sessionId: string;
  state: SessionState;
  createdAtMs: number;
  expiresAtMs: number;
  receiverTokenHash: string;
  joinTokenHash: string;
  claimNonceHash?: string;
  senderTokenHash?: string;
  offer?: {
    type: "offer";
    sdp: string;
    storedAtMs: number;
  };
  answer?: {
    type: "answer";
    sdp: string;
    storedAtMs: number;
  };
  rtcConfiguration: RtcConfigurationDto;
}
