#include "win_http_client.h"
#include <windows.h>
#include <winhttp.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
namespace km::signaling {
namespace {
std::wstring wide(std::string_view value) {
    if (value.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), int(value.size()), nullptr, 0);
    if (!length) throw std::invalid_argument("invalid UTF-8 URL/header");
    std::wstring result(size_t(length), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), int(value.size()), result.data(), length); return result;
}
std::string utf8(std::wstring_view value) {
    if (value.empty()) return {};
    const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), int(value.size()), nullptr, 0, nullptr, nullptr);
    if (!length) throw std::invalid_argument("invalid UTF-16 URL");
    std::string result(size_t(length), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), int(value.size()), result.data(), length, nullptr, nullptr); return result;
}
struct InternetClose { void operator()(void* handle) const { if (handle) WinHttpCloseHandle(handle); } };
using Internet = std::unique_ptr<void, InternetClose>;
class WinHttpTransport final : public km::IHttpTransport {
public:
    WinHttpTransport() : session_(WinHttpOpen(L"KMVirtualCamera/0.2", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)) {
        if (!session_) throw std::runtime_error("WinHttpOpen failed");
    }
    void cancel() override { ++cancellation_; }
    km::HttpResponse perform(const km::HttpRequest& input) override {
        km::HttpResponse out;
        const uint64_t epoch = cancellation_.load();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(input.timeoutMs);
        auto check = [&] {
            if (epoch != cancellation_.load()) throw std::runtime_error("HTTP request cancelled");
            if (std::chrono::steady_clock::now() >= deadline) throw std::runtime_error("HTTP deadline exceeded");
        };
        try {
            if (!input.timeoutMs || input.body.size() > 1024 * 1024 || input.url.size() > 8192 || input.url.find('#') != std::string::npos)
                throw std::invalid_argument("invalid HTTP request limits or URL");
            auto url = wide(input.url); URL_COMPONENTS c{}; c.dwStructSize = sizeof(c);
            c.dwHostNameLength = c.dwUrlPathLength = c.dwExtraInfoLength = c.dwUserNameLength = c.dwPasswordLength = DWORD(-1);
            if (!WinHttpCrackUrl(url.c_str(), DWORD(url.size()), 0, &c) || c.dwUserNameLength || c.dwPasswordLength)
                throw std::invalid_argument("invalid signaling URL");
            std::wstring host(c.lpszHostName, c.dwHostNameLength);
            const bool secure = c.nScheme == INTERNET_SCHEME_HTTPS;
            const bool loopback = host == L"localhost" || host == L"127.0.0.1" || host == L"::1" || host == L"[::1]";
            if (!secure && !(c.nScheme == INTERNET_SCHEME_HTTP && loopback)) throw std::runtime_error("signaling requires HTTPS (except explicit loopback development)");
            std::wstring path = c.dwUrlPathLength ? std::wstring(c.lpszUrlPath, c.dwUrlPathLength) : L"/";
            if (c.dwExtraInfoLength) path.append(c.lpszExtraInfo, c.dwExtraInfoLength);
            check(); Internet connection(WinHttpConnect(session_.get(), host.c_str(), c.nPort, 0));
            if (!connection) throw std::runtime_error("HTTP connection failed");
            auto method = wide(input.method);
            Internet request(WinHttpOpenRequest(connection.get(), method.c_str(), path.c_str(), nullptr,
                WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0));
            if (!request) throw std::runtime_error("HTTP request creation failed");
            const int stageTimeout = int(std::min<uint32_t>(2000, input.timeoutMs));
            if (!WinHttpSetTimeouts(request.get(), stageTimeout, stageTimeout, stageTimeout, stageTimeout))
                throw std::runtime_error("HTTP timeout configuration failed");
            DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
            if (!WinHttpSetOption(request.get(), WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects)))
                throw std::runtime_error("HTTP redirect policy failed");
            std::wstring headers;
            for (const auto& [name, value] : input.headers) {
                if (name.empty() || name.find_first_of(": \t") != std::string::npos || !safeHeader(name) || !safeHeader(value))
                    throw std::invalid_argument("invalid HTTP header");
                headers += wide(name) + L": " + wide(value) + L"\r\n";
            }
            check();
            if (!WinHttpSendRequest(request.get(), headers.c_str(), DWORD(headers.size()),
                input.body.empty() ? nullptr : const_cast<char*>(input.body.data()), DWORD(input.body.size()), DWORD(input.body.size()), 0) ||
                !WinHttpReceiveResponse(request.get(), nullptr)) throw std::runtime_error("HTTP send/receive failed");
            check(); DWORD status = 0, length = sizeof(status);
            if (!WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX, &status, &length, WINHTTP_NO_HEADER_INDEX)) throw std::runtime_error("HTTP status missing");
            DWORD declared = 0; length = sizeof(declared);
            if (WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX, &declared, &length, WINHTTP_NO_HEADER_INDEX) && declared > input.maxResponseBytes)
                throw std::runtime_error("HTTP response too large");
            std::array<char, 8192> buffer{};
            for (;;) {
                check(); DWORD count = 0;
                if (!WinHttpReadData(request.get(), buffer.data(), DWORD(buffer.size()), &count)) throw std::runtime_error("HTTP response read failed");
                if (!count) break;
                if (count > input.maxResponseBytes - out.body.size()) throw std::runtime_error("HTTP response too large");
                out.body.append(buffer.data(), count);
            }
            check(); out.status = int(status);
            if (status >= 300 && status != 404) std::cerr << "[HTTP] " << input.method << " status=" << status << "\n";
        } catch (const std::exception& e) { out.status = 0; out.body.clear(); out.error = e.what(); std::cerr << "[HTTP] " << input.method << " failed: " << e.what() << "\n"; }
        return out;
    }
private:
    Internet session_; std::atomic<uint64_t> cancellation_{0};
};
}
WinHttpClient::WinHttpClient(std::wstring base) {
    const auto first = base.find_first_not_of(L" \t\r\n\"'");
    if (first == std::wstring::npos) throw std::invalid_argument("empty signaling URL");
    const auto last = base.find_last_not_of(L" \t\r\n\"'"); base = base.substr(first, last - first + 1);
    transport_ = std::make_unique<WinHttpTransport>(); client_ = std::make_unique<SessionClient>(*transport_, utf8(base));
}
WinHttpClient::~WinHttpClient() = default;
std::optional<CreateSessionResponse> WinHttpClient::CreateSession(const std::string& name) { return client_->CreateSession(name); }
std::optional<OfferDescription> WinHttpClient::PollOffer(const std::string& id, const std::string& token) { return client_->PollOffer(id, token); }
bool WinHttpClient::PutAnswer(const std::string& id, const std::string& token, const std::string& sdp) { return client_->PutAnswer(id, token, sdp); }
bool WinHttpClient::DeleteSession(const std::string& id, const std::string& token) { return client_->DeleteSession(id, token); }
void WinHttpClient::Cancel() { transport_->cancel(); }
} // namespace km::signaling
