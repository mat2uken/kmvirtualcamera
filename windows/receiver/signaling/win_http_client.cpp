#include "win_http_client.h"
#include <vector>
#include <sstream>
#include <algorithm>

namespace km::signaling {

static std::string ExtractJsonString(const std::string& json, const std::string& key) {
    std::string pattern = "\"" + key + "\":\"";
    size_t pos = json.find(pattern);
    if (pos == std::string::npos) {
        pattern = "\"" + key + "\": \"";
        pos = json.find(pattern);
        if (pos == std::string::npos) return "";
    }
    pos += pattern.length();
    std::string result;
    bool escape = false;
    for (size_t i = pos; i < json.length(); ++i) {
        char c = json[i];
        if (escape) {
            if (c == 'n') result += '\n';
            else if (c == 'r') result += '\r';
            else if (c == 't') result += '\t';
            else if (c == '"') result += '"';
            else if (c == '\\') result += '\\';
            else result += c;
            escape = false;
        } else if (c == '\\') {
            escape = true;
        } else if (c == '"') {
            break;
        } else {
            result += c;
        }
    }
    return result;
}

static uint32_t ExtractJsonNumber(const std::string& json, const std::string& key, uint32_t defaultVal = 0) {
    std::string pattern = "\"" + key + "\":";
    size_t pos = json.find(pattern);
    if (pos == std::string::npos) return defaultVal;
    pos += pattern.length();
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    size_t end = pos;
    while (end < json.length() && (isdigit(static_cast<unsigned char>(json[end])))) end++;
    if (end > pos) {
        try {
            return static_cast<uint32_t>(std::stoul(json.substr(pos, end - pos)));
        } catch (...) {}
    }
    return defaultVal;
}

static std::string EscapeJson(const std::string& s) {
    std::string res;
    for (char c : s) {
        if (c == '"') res += "\\\"";
        else if (c == '\\') res += "\\\\";
        else if (c == '\b') res += "\\b";
        else if (c == '\f') res += "\\f";
        else if (c == '\n') res += "\\n";
        else if (c == '\r') res += "\\r";
        else if (c == '\t') res += "\\t";
        else res += c;
    }
    return res;
}

WinHttpClient::WinHttpClient(std::wstring baseUrl) {
    URL_COMPONENTS urlComp{};
    urlComp.dwStructSize = sizeof(urlComp);
    urlComp.dwHostNameLength = static_cast<DWORD>(-1);
    urlComp.dwUrlPathLength = static_cast<DWORD>(-1);
    urlComp.dwSchemeLength = static_cast<DWORD>(-1);

    if (WinHttpCrackUrl(baseUrl.c_str(), static_cast<DWORD>(baseUrl.length()), 0, &urlComp)) {
        host_ = std::wstring(urlComp.lpszHostName, urlComp.dwHostNameLength);
        port_ = urlComp.nPort;
        isHttps_ = (urlComp.nScheme == INTERNET_SCHEME_HTTPS);
    } else {
        host_ = L"127.0.0.1";
        port_ = 8787;
        isHttps_ = false;
    }

    hSession_ = WinHttpOpen(
        L"KMVirtualCamera-Receiver/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );

    if (hSession_) {
        // Set timeouts: resolve 5s, connect 5s, send 10s, receive 10s
        WinHttpSetTimeouts(hSession_, 5000, 5000, 10000, 10000);
    }
}

WinHttpClient::~WinHttpClient() {
    if (hSession_) {
        WinHttpCloseHandle(hSession_);
        hSession_ = nullptr;
    }
}

HttpResponse WinHttpClient::Request(
    const std::wstring& verb,
    const std::wstring& path,
    const std::string& bearerToken,
    const std::string& jsonBody
) {
    HttpResponse response{};
    if (!hSession_) return response;

    HINTERNET hConnect = WinHttpConnect(hSession_, host_.c_str(), port_, 0);
    if (!hConnect) return response;

    DWORD flags = isHttps_ ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, verb.c_str(), path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        return response;
    }

    std::wstring headers = L"User-Agent: KMVirtualCamera/1.0\r\n";
    if (!bearerToken.empty()) {
        headers += L"Authorization: Bearer ";
        headers += std::wstring(bearerToken.begin(), bearerToken.end());
        headers += L"\r\n";
    }
    if (!jsonBody.empty()) {
        headers += L"Content-Type: application/json\r\n";
    }

    BOOL ok = WinHttpSendRequest(
        hRequest,
        headers.c_str(),
        static_cast<DWORD>(headers.length()),
        jsonBody.empty() ? nullptr : const_cast<char*>(jsonBody.data()),
        static_cast<DWORD>(jsonBody.length()),
        static_cast<DWORD>(jsonBody.length()),
        0
    );

    if (ok && WinHttpReceiveResponse(hRequest, nullptr)) {
        DWORD statusCode = 0;
        DWORD size = sizeof(statusCode);
        WinHttpQueryHeaders(
            hRequest,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &statusCode,
            &size,
            WINHTTP_NO_HEADER_INDEX
        );
        response.statusCode = statusCode;

        wchar_t retryBuf[32] = {};
        DWORD retryLen = sizeof(retryBuf);
        if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CUSTOM, L"Retry-After", retryBuf, &retryLen, WINHTTP_NO_HEADER_INDEX)) {
            std::wstring wRetry(retryBuf, retryLen / sizeof(wchar_t));
            response.retryAfter = std::string(wRetry.begin(), wRetry.end());
        }

        // Read response body
        std::vector<char> buffer;
        DWORD bytesAvailable = 0;
        while (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0) {
            std::vector<char> temp(bytesAvailable);
            DWORD bytesRead = 0;
            if (WinHttpReadData(hRequest, temp.data(), bytesAvailable, &bytesRead) && bytesRead > 0) {
                buffer.insert(buffer.end(), temp.begin(), temp.begin() + bytesRead);
            }
        }
        response.body = std::string(buffer.begin(), buffer.end());
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    return response;
}

std::optional<CreateSessionResponse> WinHttpClient::CreateSession(const std::string& clientName) {
    std::string jsonBody = "{\"client\":{\"name\":\"" + clientName + "\",\"version\":\"0.1.0\"}}";
    auto resp = Request(L"POST", L"/v1/sessions", "", jsonBody);
    if (resp.statusCode != 201 || resp.body.empty()) {
        return std::nullopt;
    }

    CreateSessionResponse r{};
    r.sessionId = ExtractJsonString(resp.body, "sessionId");
    r.receiverToken = ExtractJsonString(resp.body, "receiverToken");
    r.joinUrl = ExtractJsonString(resp.body, "joinUrl");
    r.expiresAt = ExtractJsonString(resp.body, "expiresAt");

    r.poll.initialIntervalMs = ExtractJsonNumber(resp.body, "initialIntervalMs", 1000);
    r.poll.backoffAfterMs = ExtractJsonNumber(resp.body, "backoffAfterMs", 15000);
    r.poll.maxIntervalMs = ExtractJsonNumber(resp.body, "maxIntervalMs", 2000);
    r.poll.timeoutMs = ExtractJsonNumber(resp.body, "timeoutMs", 60000);

    // Default Cloudflare STUN server if not parsed
    IceServer s{};
    s.urls.push_back("stun:stun.cloudflare.com:3478");
    r.rtcConfiguration.iceServers.push_back(s);

    if (r.sessionId.empty() || r.receiverToken.empty() || r.joinUrl.empty()) {
        return std::nullopt;
    }
    return r;
}

std::optional<OfferDescription> WinHttpClient::PollOffer(const std::string& sessionId, const std::string& receiverToken) {
    std::wstring path = L"/v1/sessions/" + std::wstring(sessionId.begin(), sessionId.end()) + L"/offer";
    auto resp = Request(L"GET", path, receiverToken);

    if (resp.statusCode == 200 && !resp.body.empty()) {
        OfferDescription offer{};
        offer.type = ExtractJsonString(resp.body, "type");
        offer.sdp = ExtractJsonString(resp.body, "sdp");
        if (!offer.sdp.empty()) {
            return offer;
        }
    }
    return std::nullopt;
}

bool WinHttpClient::PutAnswer(const std::string& sessionId, const std::string& receiverToken, const std::string& sdp) {
    std::wstring path = L"/v1/sessions/" + std::wstring(sessionId.begin(), sessionId.end()) + L"/answer";
    std::string jsonBody = "{\"type\":\"answer\",\"sdp\":\"" + EscapeJson(sdp) + "\"}";
    auto resp = Request(L"PUT", path, receiverToken, jsonBody);
    return (resp.statusCode == 204);
}

bool WinHttpClient::DeleteSession(const std::string& sessionId, const std::string& token) {
    std::wstring path = L"/v1/sessions/" + std::wstring(sessionId.begin(), sessionId.end());
    auto resp = Request(L"DELETE", path, token);
    return (resp.statusCode == 204);
}

} // namespace km::signaling
