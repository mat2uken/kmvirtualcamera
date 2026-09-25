// WinHTTP failure-path coverage for the signaling transport:
//   - normal loopback 201 (positive control for the in-process mock server)
//   - declared response body larger than the 1 MiB byte limit
//   - 302 redirect under WINHTTP_OPTION_REDIRECT_POLICY_NEVER
//   - request deadline (trickled body outliving timeoutMs)
//   - mid-flight cancellation via Cancel()
//   - TLS certificate error against an expired certificate host
//   - plain-HTTP rejection outside explicit loopback development
// Each scenario captures the [HTTP] line the transport writes to stderr and
// asserts the failure-path-specific message, so the recorded ctest output is
// the evidence for every path.
#include "../receiver/signaling/win_http_client.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <chrono>
#include <future>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

#pragma comment(lib, "ws2_32.lib")

namespace {
int failures = 0;
void check(bool ok, const std::string& name, const std::string& detail = {}) {
    if (ok) {
        std::cout << "[PASS] " << name << std::endl;
    } else {
        std::cout << "[FAIL] " << name << (detail.empty() ? "" : (": " + detail)) << std::endl;
        ++failures;
    }
}

std::string headers(const std::string& statusLine, const std::string& extra, size_t contentLength) {
    return statusLine + "\r\n" + extra + "Content-Length: " + std::to_string(contentLength) +
        "\r\nConnection: close\r\n\r\n";
}

// Serves exactly one HTTP request on 127.0.0.1:<ephemeral>. `response` is sent
// as soon as the request head arrives; each trickle byte follows after
// trickleIntervalMs so per-stage socket timeouts stay satisfied while the
// overall request deadline keeps running.
void serveOnce(std::string response, int trickleCount, int trickleIntervalMs, std::promise<UINT>& port) {
    SOCKET listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) { port.set_value(0); return; }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (::bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
        ::listen(listener, 1) != 0) {
        port.set_value(0);
        closesocket(listener);
        return;
    }
    sockaddr_in bound{};
    int len = sizeof(bound);
    ::getsockname(listener, reinterpret_cast<sockaddr*>(&bound), &len);
    port.set_value(ntohs(bound.sin_port));

    fd_set ready;
    FD_ZERO(&ready);
    FD_SET(listener, &ready);
    timeval acceptWindow{20, 0};
    if (::select(0, &ready, nullptr, nullptr, &acceptWindow) > 0) {
        SOCKET conn = ::accept(listener, nullptr, nullptr);
        if (conn != INVALID_SOCKET) {
            timeval io{10, 0};
            setsockopt(conn, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char*>(&io), static_cast<int>(sizeof(io)));
            std::string head;
            char buf[4096];
            while (head.find("\r\n\r\n") == std::string::npos && head.size() < 16384) {
                int n = ::recv(conn, buf, static_cast<int>(sizeof(buf)), 0);
                if (n <= 0) break;
                head.append(buf, static_cast<size_t>(n));
            }
            if (!response.empty()) ::send(conn, response.data(), static_cast<int>(response.size()), 0);
            for (int i = 0; i < trickleCount; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(trickleIntervalMs));
                const char byte = 'x';
                if (::send(conn, &byte, 1, 0) <= 0) break;
            }
            closesocket(conn);
        }
    }
    closesocket(listener);
}

struct MockServer {
    explicit MockServer(std::string response, int trickleCount = 0, int trickleIntervalMs = 0)
        : portFuture(portPromise.get_future()),
          worker(serveOnce, std::move(response), trickleCount, trickleIntervalMs, std::ref(portPromise)) {}
    ~MockServer() {
        if (worker.joinable()) worker.join();
    }
    std::promise<UINT> portPromise;
    std::future<UINT> portFuture;
    std::thread worker;
};

const char kSessionJson[] =
    R"({"sessionId":"winHttpPathsSession01","receiverToken":"tok-win-http-paths","joinUrl":"https://example.invalid/send/#v=1&s=x","expiresAt":"2026-09-26T00:00:00Z","poll":{"initialIntervalMs":500,"backoffAfterMs":5000,"maxIntervalMs":5000,"timeoutMs":600000},"rtcConfiguration":{"iceTransportPolicy":"all","iceServers":[]}})";

struct SessionResult {
    bool ok = false;
    std::string errLog; // the [HTTP] lines the transport wrote during the call
};

// Runs CreateSession while capturing the transport's stderr, then replays the
// capture so ctest output keeps the [HTTP] evidence.
SessionResult createSession(km::signaling::WinHttpClient& client) {
    SessionResult out;
    std::stringstream captured;
    std::streambuf* previous = std::cerr.rdbuf(captured.rdbuf());
    auto resp = client.CreateSession("win-http-paths-test");
    std::cerr.rdbuf(previous);
    out.ok = resp.has_value();
    out.errLog = captured.str();
    std::cerr << out.errLog;
    return out;
}

SessionResult createOn(UINT port) {
    km::signaling::WinHttpClient client(L"http://127.0.0.1:" + std::to_wstring(port));
    return createSession(client);
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}
} // namespace

int main() {
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cout << "[FAIL] WSAStartup failed" << std::endl;
        return 1;
    }

    {
        // Positive control: a well-formed 201 body must decode with no failure line.
        const std::string body(kSessionJson);
        MockServer server(headers("HTTP/1.1 201 Created", "Content-Type: application/json\r\n", body.size()) + body);
        const UINT port = server.portFuture.get();
        const SessionResult r = port != 0 ? createOn(port) : SessionResult{};
        check(port != 0 && r.ok && !contains(r.errLog, "failed"), "normal-201-loopback");
    }
    {
        // Declared body above the 1 MiB byte limit is rejected before reading.
        MockServer server(headers("HTTP/1.1 201 Created", "", 2 * 1024 * 1024));
        const UINT port = server.portFuture.get();
        const SessionResult r = port != 0 ? createOn(port) : SessionResult{};
        check(port != 0 && !r.ok, "declared-overflow-over-1MiB");
        check(contains(r.errLog, "HTTP response too large"), "overflow-error-string", r.errLog);
    }
    {
        // Redirect policy NEVER surfaces the 302 as a non-2xx status.
        MockServer server(
            "HTTP/1.1 302 Found\r\nLocation: http://127.0.0.1/elsewhere\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        const UINT port = server.portFuture.get();
        const SessionResult r = port != 0 ? createOn(port) : SessionResult{};
        check(port != 0 && !r.ok, "redirect-302-not-followed");
        check(contains(r.errLog, "status=302"), "redirect-error-string", r.errLog);
    }
    {
        // Trickle one byte every 1.5s: each receive stage succeeds, but the
        // 10s overall deadline fires inside the read loop.
        MockServer server(headers("HTTP/1.1 201 Created", "", 100), 9, 1500);
        const UINT port = server.portFuture.get();
        const auto began = std::chrono::steady_clock::now();
        const SessionResult r = port != 0 ? createOn(port) : SessionResult{};
        const auto tookMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - began).count();
        std::cout << "  deadline scenario elapsedMs=" << tookMs << std::endl;
        check(port != 0 && !r.ok, "deadline-exceeded-at-timeoutMs");
        check(contains(r.errLog, "HTTP deadline exceeded"), "deadline-error-string", r.errLog);
        check(tookMs >= 9000 && tookMs < 30000, "deadline-timing-9s-30s", "elapsedMs=" + std::to_string(tookMs));
    }
    {
        // Cancel() from a second thread while the body trickles; the epoch
        // check must abort the request. WinHttpReadData keeps waiting for the
        // full body until the server closes (~9s), so the abort is proven by
        // the "HTTP request cancelled" line rather than by timing.
        MockServer server(headers("HTTP/1.1 201 Created", "", 100), 6, 1500);
        const UINT port = server.portFuture.get();
        check(port != 0, "cancel-server-up");
        if (port != 0) {
            km::signaling::WinHttpClient client(L"http://127.0.0.1:" + std::to_wstring(port));
            std::thread canceller([&client] {
                std::this_thread::sleep_for(std::chrono::milliseconds(3000));
                client.Cancel();
            });
            const auto began = std::chrono::steady_clock::now();
            const SessionResult r = createSession(client);
            const auto tookMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - began).count();
            canceller.join();
            std::cout << "  cancel scenario elapsedMs=" << tookMs << std::endl;
            check(!r.ok, "cancel-midflight");
            check(contains(r.errLog, "HTTP request cancelled"), "cancel-error-string", r.errLog);
        }
    }
    {
        // Certificate validation failure against a known-expired host.
        km::signaling::WinHttpClient client(L"https://expired.badssl.com");
        const SessionResult r = createSession(client);
        check(!r.ok, "certificate-error-expired-host");
        check(contains(r.errLog, "HTTP send/receive failed"), "certificate-error-string", r.errLog);
    }
    {
        // Plain HTTP is rejected before any network I/O outside loopback.
        km::signaling::WinHttpClient client(L"http://example.com");
        const SessionResult r = createSession(client);
        check(!r.ok, "plain-http-rejected-non-loopback");
        check(contains(r.errLog, "signaling requires HTTPS"), "plain-http-error-string", r.errLog);
    }

    WSACleanup();
    std::cout << (failures == 0 ? "RESULT: PASS" : "RESULT: FAIL") << " scenarios-failures=" << failures << std::endl;
    return failures == 0 ? 0 : 1;
}
