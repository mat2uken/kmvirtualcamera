#pragma once

#include <windows.h>
#include <winhttp.h>
#include <string>
#include <optional>
#include "signaling_models.h"

namespace km::signaling {

struct HttpResponse {
    DWORD statusCode = 0;
    std::string body;
    std::string retryAfter;
};

class WinHttpClient {
public:
    explicit WinHttpClient(std::wstring baseUrl = L"http://127.0.0.1:8787");
    ~WinHttpClient();

    std::optional<CreateSessionResponse> CreateSession(const std::string& clientName = "windows-receiver");
    std::optional<OfferDescription> PollOffer(const std::string& sessionId, const std::string& receiverToken);
    bool PutAnswer(const std::string& sessionId, const std::string& receiverToken, const std::string& sdp);
    bool DeleteSession(const std::string& sessionId, const std::string& token);

private:
    HttpResponse Request(
        const std::wstring& verb,
        const std::wstring& path,
        const std::string& bearerToken = "",
        const std::string& jsonBody = ""
    );

    std::wstring host_;
    INTERNET_PORT port_{8787};
    bool isHttps_{false};
    HINTERNET hSession_{nullptr};
};

} // namespace km::signaling
