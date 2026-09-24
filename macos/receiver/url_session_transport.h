#pragma once
#include "km/receiver_contracts.h"
#include "receiver/signaling/session_client.h"
#include <memory>
#include <optional>
#include <string>

namespace km::mac {
// NSURLSession-backed blocking transport for the signaling worker ONLY.
// perform() parks the calling thread until response, deadline or cancel(); it
// must never run on the main thread, an RTC callback or the video worker.
// HTTPS is required except for explicit loopback development URLs, redirects
// are never followed and the response byte cap is enforced while receiving.
std::unique_ptr<km::IHttpTransport> MakeUrlSessionTransport();

// Thin SessionClient wrapper mirroring windows's WinHttpClient.
class MacHttpClient {
public:
    explicit MacHttpClient(std::string baseUrl);
    std::optional<km::signaling::CreateSessionResponse> CreateSession(const std::string& name = "macos-receiver");
    std::optional<km::signaling::OfferDescription> PollOffer(const std::string& id, const std::string& token);
    bool PutAnswer(const std::string& id, const std::string& token, const std::string& sdp);
    bool DeleteSession(const std::string& id, const std::string& token);
    void Cancel();

private:
    std::unique_ptr<km::IHttpTransport> transport_;
    std::unique_ptr<km::signaling::SessionClient> client_;
};
} // namespace km::mac
