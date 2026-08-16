#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace km::signaling {

struct IceServer {
    std::vector<std::string> urls;
    std::string username;
    std::string credential;
};

struct RtcConfiguration {
    std::vector<IceServer> iceServers;
    std::string iceTransportPolicy = "all";
};

struct PollPolicy {
    uint32_t initialIntervalMs = 1000;
    uint32_t backoffAfterMs = 15000;
    uint32_t maxIntervalMs = 2000;
    uint32_t timeoutMs = 60000;
};

struct CreateSessionResponse {
    std::string sessionId;
    std::string receiverToken;
    std::string joinUrl;
    std::string expiresAt;
    PollPolicy poll;
    RtcConfiguration rtcConfiguration;
};

struct OfferDescription {
    std::string type = "offer";
    std::string sdp;
};

struct AnswerDescription {
    std::string type = "answer";
    std::string sdp;
};

} // namespace km::signaling
