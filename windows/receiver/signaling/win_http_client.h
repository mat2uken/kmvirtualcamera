#pragma once
#include <memory>
#include <optional>
#include <string>
#include "signaling_models.h"
#include "../../../shared/km/receiver_contracts.h"
#include "../../../shared/receiver/signaling/session_client.h"
namespace km::signaling {
class WinHttpClient {
public:
    explicit WinHttpClient(std::wstring baseUrl = L"http://127.0.0.1:8787");
    ~WinHttpClient();
    std::optional<CreateSessionResponse> CreateSession(const std::string& name = "windows-receiver");
    std::optional<OfferDescription> PollOffer(const std::string& id, const std::string& token);
    bool PutAnswer(const std::string& id, const std::string& token, const std::string& sdp);
    bool DeleteSession(const std::string& id, const std::string& token);
    void Cancel();
private:
    std::unique_ptr<km::IHttpTransport> transport_;
    std::unique_ptr<SessionClient> client_;
};
} // namespace km::signaling
