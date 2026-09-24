// Loopback-only test for the macOS signaling transport (M4-a).
// The server binds 127.0.0.1 only: no external network, no install/sign.
// Covers: HTTPS-only policy, header/URL validation, the full SessionClient
// flow (create/offer/answer/delete), redirect refusal, the 1MiB response cap
// (declared and streaming), deadline, and cancel semantics.
#include "receiver/url_session_transport.h"
#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            std::cerr << __LINE__ << ": " #x "\n";                                                                     \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (false)

namespace {
const std::string kSessionJson =
    R"({"sessionId":"abc-123","receiverToken":"secret","joinUrl":"https://example.test/send/abc","expiresAt":"2026-09-23","poll":{"initialIntervalMs":1000,"backoffAfterMs":15000,"maxIntervalMs":2000,"timeoutMs":60000},"rtcConfiguration":{"iceTransportPolicy":"relay","iceServers":[{"urls":["turn:[2001:db8::1]:3478?transport=udp"],"username":"user","credential":"password"}]}})";
const std::string kOfferJson = R"({"type":"offer","sdp":"v=0\r\n"})";

std::string Lower(std::string value) {
    for (char& c : value)
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return value;
}

class LoopbackServer {
public:
    LoopbackServer() {
        signal(SIGPIPE, SIG_IGN);
        listen_ = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // loopback only
        addr.sin_port = 0;
        if (::bind(listen_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || ::listen(listen_, 8) != 0) {
            failed_ = true;
            return;
        }
        socklen_t len = sizeof(addr);
        ::getsockname(listen_, reinterpret_cast<sockaddr*>(&addr), &len);
        port_ = ntohs(addr.sin_port);
        thread_ = std::thread([this] { Run(); });
    }
    ~LoopbackServer() {
        stop_ = true;
        if (thread_.joinable()) thread_.join();
        if (listen_ >= 0) ::close(listen_);
    }
    bool ok() const { return !failed_; }
    std::string base() const { return "http://127.0.0.1:" + std::to_string(port_); }
    int okHits() const { return okHits_.load(); }
    std::string lastMethodPath() const { return Access(lastMethodPath_); }
    std::string lastAuth() const { return Access(lastAuth_); }
    std::string lastBody() const { return Access(lastBody_); }

private:
    std::string Access(const std::string& value) const {
        std::lock_guard lock(mutex_);
        return value;
    }
    void Record(std::string methodPath, std::string auth, std::string body) {
        std::lock_guard lock(mutex_);
        lastMethodPath_ = std::move(methodPath);
        lastAuth_ = std::move(auth);
        lastBody_ = std::move(body);
    }
    static bool Send(int fd, const std::string& data) {
        size_t sent = 0;
        while (sent < data.size()) {
            const ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, 0);
            if (n <= 0) return false;
            sent += size_t(n);
        }
        return true;
    }
    static void Respond(int fd, int status, const char* reason, const std::string& body,
                        const std::vector<std::string>& extra = {}) {
        std::string out = "HTTP/1.1 " + std::to_string(status) + " " + reason +
            "\r\nContent-Length: " + std::to_string(body.size()) + "\r\nConnection: close";
        for (const std::string& header : extra) out += "\r\n" + header;
        out += "\r\n\r\n";
        out += body;
        Send(fd, out);
    }
    void Run() {
        while (!stop_) {
            pollfd item{listen_, POLLIN, 0};
            if (::poll(&item, 1, 100) <= 0) continue;
            const int fd = ::accept(listen_, nullptr, nullptr);
            if (fd < 0) continue;
            Handle(fd);
            ::close(fd);
        }
    }
    void Handle(int fd) {
        std::string data;
        char buffer[4096];
        size_t headerEnd = std::string::npos;
        while ((headerEnd = data.find("\r\n\r\n")) == std::string::npos) {
            const ssize_t n = ::recv(fd, buffer, sizeof(buffer), 0);
            if (n <= 0) return;
            data.append(buffer, size_t(n));
            if (data.size() > 2 * 1024 * 1024) return;
        }
        const std::string head = data.substr(0, headerEnd);
        const size_t lineEnd = head.find("\r\n");
        if (lineEnd == std::string::npos) return;
        const std::string requestLine = head.substr(0, lineEnd);
        const size_t methodEnd = requestLine.find(' ');
        const size_t pathBegin = methodEnd == std::string::npos ? methodEnd : requestLine.find(' ', methodEnd + 1);
        if (methodEnd == std::string::npos || pathBegin == std::string::npos) return;
        const std::string method = requestLine.substr(0, methodEnd);
        const std::string path = requestLine.substr(methodEnd + 1, pathBegin - methodEnd - 1);

        size_t contentLength = 0;
        std::string auth;
        size_t cursor = lineEnd + 2;
        while (cursor < head.size()) {
            const size_t next = head.find("\r\n", cursor);
            const std::string line = head.substr(cursor, next == std::string::npos ? std::string::npos : next - cursor);
            const size_t colon = line.find(':');
            if (colon != std::string::npos) {
                const std::string name = Lower(line.substr(0, colon));
                size_t valueBegin = colon + 1;
                while (valueBegin < line.size() && (line[valueBegin] == ' ' || line[valueBegin] == '\t')) ++valueBegin;
                const std::string value = line.substr(valueBegin);
                if (name == "content-length") contentLength = strtoul(value.c_str(), nullptr, 10);
                if (name == "authorization") auth = value;
            }
            if (next == std::string::npos) break;
            cursor = next + 2;
        }
        std::string body = data.substr(headerEnd + 4);
        while (body.size() < contentLength) {
            const ssize_t n = ::recv(fd, buffer, sizeof(buffer), 0);
            if (n <= 0) return;
            body.append(buffer, size_t(n));
        }
        body.resize(contentLength);
        Record(method + " " + path, auth, body);

        if (method == "POST" && path == "/v1/sessions") {
            Respond(fd, 201, "Created", kSessionJson);
        } else if (method == "GET" && path == "/v1/sessions/abc-123/offer") {
            Respond(fd, 200, "OK", kOfferJson);
        } else if (method == "PUT" && path == "/v1/sessions/abc-123/answer") {
            Respond(fd, 204, "No Content", "");
        } else if (method == "DELETE" && path == "/v1/sessions/abc-123") {
            Respond(fd, 204, "No Content", "");
        } else if (method == "GET" && path == "/redirect") {
            Respond(fd, 302, "Found", "", {"Location: /ok"});
        } else if (method == "GET" && path == "/ok") {
            ++okHits_;
            Respond(fd, 200, "OK", "ok");
        } else if (method == "GET" && path == "/big") {
            const std::string chunk(64 * 1024, 'x');
            const std::string header = "HTTP/1.1 200 OK\r\nContent-Length: 2097152\r\nConnection: close\r\n\r\n";
            if (!Send(fd, header)) return;
            for (int i = 0; i < 32; ++i)
                if (!Send(fd, chunk)) return;
        } else if (method == "GET" && path == "/bigstream") {
            // No Content-Length: the cap must be enforced while receiving.
            const std::string chunk(64 * 1024, 'y');
            if (!Send(fd, "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n")) return;
            for (int i = 0; i < 32; ++i)
                if (!Send(fd, chunk)) return;
        } else if (method == "GET" && path == "/slow") {
            std::this_thread::sleep_for(std::chrono::milliseconds(1500));
            Respond(fd, 200, "OK", "slow");
        } else {
            Respond(fd, 404, "Not Found", "");
        }
    }
    int listen_ = -1;
    uint16_t port_ = 0;
    bool failed_ = false;
    std::atomic<bool> stop_{false};
    std::atomic<int> okHits_{0};
    std::thread thread_;
    mutable std::mutex mutex_;
    std::string lastMethodPath_, lastAuth_, lastBody_;
};

km::HttpRequest Get(const std::string& url, uint32_t timeoutMs = 5000) {
    km::HttpRequest request;
    request.method = "GET";
    request.url = url;
    request.timeoutMs = timeoutMs;
    return request;
}
} // namespace

int main() {
    LoopbackServer server;
    CHECK(server.ok());

    // Plain HTTP is rejected before any network I/O, except explicit loopback.
    {
        auto transport = km::mac::MakeUrlSessionTransport();
        const km::HttpResponse response = transport->perform(Get("http://example.com/v1/sessions"));
        CHECK(response.status == 0);
        CHECK(response.error.find("HTTPS") != std::string::npos);
    }
    // Fragment URLs and control-character header values are rejected.
    {
        auto transport = km::mac::MakeUrlSessionTransport();
        CHECK(transport->perform(Get(server.base() + "/ok#frag")).error ==
            "invalid HTTP request limits or URL");
        km::HttpRequest request = Get(server.base() + "/ok");
        request.headers.emplace_back("X-Bad", "v\nv");
        CHECK(transport->perform(request).error == "invalid HTTP header");
    }

    // Full SessionClient flow through the Mac adapter: create, offer poll,
    // answer put, delete, with bearer auth and TURN-bearing RTC config.
    {
        km::mac::MacHttpClient client(server.base() + "/");
        const auto session = client.CreateSession("mac-test");
        CHECK(session);
        CHECK(session->sessionId == "abc-123");
        CHECK(session->receiverToken == "secret");
        CHECK(session->rtcConfiguration.iceTransportPolicy == "relay");
        CHECK(session->rtcConfiguration.iceServers.size() == 1);
        CHECK(session->rtcConfiguration.iceServers[0].urls.size() == 1);
        CHECK(server.lastMethodPath() == "POST /v1/sessions");
        CHECK(server.lastAuth().empty());
        CHECK(server.lastBody().find("mac-test") != std::string::npos);

        const auto offer = client.PollOffer(session->sessionId, session->receiverToken);
        CHECK(offer);
        CHECK(offer->sdp == "v=0\r\n");
        CHECK(server.lastMethodPath() == "GET /v1/sessions/abc-123/offer");
        CHECK(server.lastAuth() == "Bearer secret");

        CHECK(client.PutAnswer(session->sessionId, session->receiverToken, "v=0\r\n"));
        CHECK(server.lastMethodPath() == "PUT /v1/sessions/abc-123/answer");
        CHECK(client.DeleteSession(session->sessionId, session->receiverToken));
        CHECK(server.lastMethodPath() == "DELETE /v1/sessions/abc-123");
    }

    // Redirects are never followed: the redirect target must stay untouched.
    {
        auto transport = km::mac::MakeUrlSessionTransport();
        const km::HttpResponse response = transport->perform(Get(server.base() + "/redirect"));
        CHECK(response.status == 302 || !response.error.empty());
        CHECK(server.okHits() == 0);
    }

    // 1MiB response cap: declared Content-Length and streaming both refuse.
    {
        auto transport = km::mac::MakeUrlSessionTransport();
        const km::HttpResponse declared = transport->perform(Get(server.base() + "/big"));
        CHECK(declared.status == 0);
        CHECK(declared.error == "HTTP response too large");
        CHECK(declared.body.empty());
        const km::HttpResponse streamed = transport->perform(Get(server.base() + "/bigstream"));
        CHECK(streamed.status == 0);
        CHECK(streamed.error == "HTTP response too large");
        CHECK(streamed.body.empty());
    }

    // Deadline: an idle server beyond timeoutMs fails with the deadline error.
    {
        auto transport = km::mac::MakeUrlSessionTransport();
        const km::HttpResponse response = transport->perform(Get(server.base() + "/slow", 400));
        CHECK(response.status == 0);
        CHECK(response.error == "HTTP deadline exceeded");
    }

    // Cancel aborts the in-flight request and keeps the transport cancelled.
    {
        auto transport = km::mac::MakeUrlSessionTransport();
        km::HttpResponse result;
        std::thread worker([&] { result = transport->perform(Get(server.base() + "/slow", 10000)); });
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        transport->cancel();
        worker.join();
        CHECK(result.status == 0);
        CHECK(result.error == "HTTP request cancelled");
        const km::HttpResponse after = transport->perform(Get(server.base() + "/ok"));
        CHECK(after.status == 0);
        CHECK(after.error == "HTTP request cancelled");
        CHECK(server.okHits() == 0);
    }

    std::cout << "url_session_transport: all checks passed\n";
    return 0;
}
