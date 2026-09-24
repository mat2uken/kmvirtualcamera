#include "receiver/signaling/signaling_worker.h"
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <iostream>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ":" << __LINE__ << ": " #x "\n"; return 1; } } while (false)
using km::signaling::SignalingPhase;
using km::signaling::SignalingWorker;
using P = SignalingPhase;

namespace {
constexpr int64_t kMs = 1'000'000;
// Offer bodies as the server sends them (JSON escapes) and the SDP after decoding.
const std::string kOfferBody =
    "{\"type\":\"offer\",\"sdp\":\"v=0\\r\\no=- 1 1 IN IP4 0.0.0.0\\r\\n\"}";
const std::string kOfferSdp = "v=0\r\no=- 1 1 IN IP4 0.0.0.0\r\n";
const std::string kOfferBodySmall = "{\"type\":\"offer\",\"sdp\":\"v=0\\r\\n\"}";

// Poll values must satisfy session_codec.h's policy validation
// (initial >= 20, max >= initial, timeout >= max, backoff <= timeout).
std::string sessionBody(uint32_t initialMs, uint32_t backoffAfterMs, uint32_t maxMs, uint32_t timeoutMs) {
    return std::string(R"({"sessionId":"sess-42","receiverToken":"tok-abc",)") +
        R"("joinUrl":"https://example.test/send/sess-42",)" +
        R"("expiresAt":"2026-09-24T00:00:00Z","poll":{)" +
        "\"initialIntervalMs\":" + std::to_string(initialMs) +
        ",\"backoffAfterMs\":" + std::to_string(backoffAfterMs) +
        ",\"maxIntervalMs\":" + std::to_string(maxMs) +
        ",\"timeoutMs\":" + std::to_string(timeoutMs) +
        R"(},"rtcConfiguration":{"iceTransportPolicy":"all","iceServers":[]}})";
}

// Scripted transport: logs requests and performing threads, and can block an offer
// poll until cancel() arrives (the worker's interruption path).
struct FakeTransport : km::IHttpTransport {
    mutable std::mutex mu;
    std::condition_variable cv;
    std::vector<km::HttpRequest> requests;
    std::set<std::thread::id> performThreads;
    bool canceled = false;
    bool blockOffers = false; // set before start(): worker-thread reads only
    bool offerBlocked = false;
    std::function<km::HttpResponse(const km::HttpRequest&)> responder;

    km::HttpResponse perform(const km::HttpRequest& r) override {
        {
            std::lock_guard<std::mutex> lock(mu);
            requests.push_back(r);
            performThreads.insert(std::this_thread::get_id());
        }
        if (blockOffers && r.method == "GET" && r.url.ends_with("/offer")) {
            std::unique_lock<std::mutex> lock(mu);
            offerBlocked = true;
            cv.notify_all();
            cv.wait(lock, [this] { return canceled; });
            return {0, "", "canceled", {}};
        }
        if (responder) return responder(r);
        return {};
    }
    void cancel() override {
        {
            std::lock_guard<std::mutex> lock(mu);
            canceled = true;
        }
        cv.notify_all();
    }
    int count(const std::string& method, const std::string& suffix) const {
        std::lock_guard<std::mutex> lock(mu);
        int n = 0;
        for (const auto& r : requests)
            if (r.method == method && r.url.ends_with(suffix)) ++n;
        return n;
    }
};

// Records every callback plus the thread it ran on. The factory can be told to fail
// (RTC answer unavailable).
struct Recorder {
    mutable std::mutex mu;
    std::vector<P> phases;
    std::vector<std::string> details;
    std::vector<std::thread::id> callbackThreads;
    km::signaling::CreateSessionResponse session;
    int sessions = 0;
    int answers = 0;
    bool answerUnavailable = false;
    std::string offerSdp;
    std::string answerSdp = "v=0\r\na=recvonly\r\n";

    SignalingWorker::Callbacks callbacks() {
        SignalingWorker::Callbacks cb;
        cb.onPhase = [this](P phase, const std::string& detail) {
            std::lock_guard<std::mutex> lock(mu);
            phases.push_back(phase);
            details.push_back(detail);
            callbackThreads.push_back(std::this_thread::get_id());
        };
        cb.onSessionCreated = [this](const km::signaling::CreateSessionResponse& s) {
            std::lock_guard<std::mutex> lock(mu);
            session = s;
            ++sessions;
            callbackThreads.push_back(std::this_thread::get_id());
        };
        cb.makeAnswer = [this](const std::string& sdp, const auto&) -> std::optional<std::string> {
            std::lock_guard<std::mutex> lock(mu);
            offerSdp = sdp;
            ++answers;
            callbackThreads.push_back(std::this_thread::get_id());
            return answerUnavailable ? std::optional<std::string>{} : std::optional<std::string>(answerSdp);
        };
        return cb;
    }
};

// Virtual monotonic clock: sleeps advance time instantly, so backoff and timeout
// are asserted exactly without real waiting. Touched only by the worker thread;
// read by the test after join.
struct VirtualClock {
    int64_t nowNs = 0;
    km::signaling::SignalingTimeSource source() {
        km::signaling::SignalingTimeSource time;
        time.nowMonotonicNs = [this] { return nowNs; };
        time.sleepNs = [this](int64_t durationNs) { nowNs += durationNs; };
        return time;
    }
};

bool waitFinished(SignalingWorker& worker, std::chrono::milliseconds limit) {
    const auto until = std::chrono::steady_clock::now() + limit;
    while (worker.running() && std::chrono::steady_clock::now() < until)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return !worker.running();
}

// Every callback and every blocking perform must share one thread, and it must not
// be the test's (main) thread.
bool oneWorkerThread(const std::vector<std::thread::id>& callbackIds, const std::set<std::thread::id>& performIds) {
    if (callbackIds.empty() || performIds.empty()) return false;
    const std::thread::id expected = callbackIds.front();
    for (const auto& id : callbackIds)
        if (id != expected) return false;
    for (const auto& id : performIds)
        if (id != expected) return false;
    return expected != std::this_thread::get_id();
}
} // namespace

int main() {
    // 1: happy path - create, poll with server interval, answer, PUT; thread affinity.
    {
        FakeTransport transport;
        Recorder rec;
        VirtualClock clock;
        int offerPolls = 0;
        transport.responder = [&](const km::HttpRequest& r) -> km::HttpResponse {
            if (r.method == "POST" && r.url.ends_with("/v1/sessions"))
                return {201, sessionBody(1000, 15000, 2000, 60000), "", {}};
            if (r.method == "GET" && r.url.ends_with("/offer")) {
                if (++offerPolls <= 3) return {404, "", "", {}};
                return {200, kOfferBody, "", {}};
            }
            if (r.method == "PUT" && r.url.ends_with("/answer")) return {204, "", "", {}};
            return {0, "", "unexpected request", {}};
        };
        SignalingWorker worker(transport, "https://example.test/prefix/", rec.callbacks(), clock.source());
        CHECK(worker.start("macos-receiver"));
        CHECK(waitFinished(worker, std::chrono::seconds(5)));
        CHECK(!worker.cancel()); // completed on its own; cancel only reaps the thread
        CHECK(rec.phases == (std::vector<P>{P::CreatingSession, P::SessionCreated, P::WaitingForOffer,
                P::GeneratingAnswer, P::SendingAnswer, P::Succeeded}));
        CHECK(offerPolls == 4);
        CHECK(clock.nowNs == 3000 * kMs); // polls at 0/1000/2000/3000ms, no backoff before 15000ms
        CHECK(rec.sessions == 1);
        CHECK(rec.session.sessionId == "sess-42");
        CHECK(rec.session.joinUrl == "https://example.test/send/sess-42");
        CHECK(rec.answers == 1);
        CHECK(rec.offerSdp == kOfferSdp);
        CHECK(transport.count("POST", "/v1/sessions") == 1);
        CHECK(transport.count("PUT", "/answer") == 1);
        {
            std::lock_guard<std::mutex> lock(transport.mu);
            std::string answerBody;
            for (const auto& r : transport.requests)
                if (r.method == "PUT" && r.url.ends_with("/answer")) answerBody = r.body;
            CHECK(!answerBody.empty());
            const auto parsed = km::json::parse(answerBody);
            CHECK(parsed.at("type").string() == "answer");
            CHECK(parsed.at("sdp").string() == rec.answerSdp);
        }
        CHECK(oneWorkerThread(rec.callbackThreads, transport.performThreads));
    }
    // 2: backoff clamp and timeout are exact under virtual time - polls at
    // 0/100/200/300/400/500/600ms (interval 100), then min(100+500,250)=250ms
    // in 20ms slices (260ms) at 860ms, then timeout at 1120ms.
    {
        FakeTransport transport;
        Recorder rec;
        VirtualClock clock;
        transport.responder = [&](const km::HttpRequest& r) -> km::HttpResponse {
            if (r.method == "POST") return {201, sessionBody(100, 500, 250, 1050), "", {}};
            return {404, "", "", {}};
        };
        SignalingWorker worker(transport, "https://example.test", rec.callbacks(), clock.source());
        CHECK(worker.start());
        CHECK(waitFinished(worker, std::chrono::seconds(5)));
        CHECK(!worker.cancel());
        CHECK(rec.phases == (std::vector<P>{P::CreatingSession, P::SessionCreated, P::WaitingForOffer, P::TimedOut}));
        CHECK(transport.count("GET", "/offer") == 8);
        CHECK(clock.nowNs == 1120 * kMs);
        CHECK(rec.answers == 0);
    }
    // 3: create failure is terminal and never reaches the offer poll.
    {
        FakeTransport transport;
        Recorder rec;
        VirtualClock clock;
        transport.responder = [](const km::HttpRequest&) -> km::HttpResponse { return {500, "", "", {}}; };
        SignalingWorker worker(transport, "https://example.test", rec.callbacks(), clock.source());
        CHECK(worker.start());
        CHECK(waitFinished(worker, std::chrono::seconds(5)));
        CHECK(!worker.cancel());
        CHECK(rec.phases == (std::vector<P>{P::CreatingSession, P::Failed}));
        CHECK(rec.details.back() == "session creation failed");
        CHECK(transport.count("GET", "/offer") == 0);
        CHECK(rec.sessions == 0);
    }
    // 4: unavailable RTC answer fails the run without sending anything.
    {
        FakeTransport transport;
        Recorder rec;
        VirtualClock clock;
        rec.answerUnavailable = true;
        transport.responder = [&](const km::HttpRequest& r) -> km::HttpResponse {
            if (r.method == "POST") return {201, sessionBody(1000, 15000, 2000, 60000), "", {}};
            if (r.method == "GET") return {200, kOfferBodySmall, "", {}};
            return {0, "", "unexpected request", {}};
        };
        SignalingWorker worker(transport, "https://example.test", rec.callbacks(), clock.source());
        CHECK(worker.start());
        CHECK(waitFinished(worker, std::chrono::seconds(5)));
        CHECK(!worker.cancel());
        CHECK(rec.phases == (std::vector<P>{P::CreatingSession, P::SessionCreated, P::WaitingForOffer,
                P::GeneratingAnswer, P::Failed}));
        CHECK(rec.details.back() == "answer generation failed");
        CHECK(transport.count("PUT", "/answer") == 0);
        CHECK(clock.nowNs == 0); // offer arrived on the first immediate poll
    }
    // 5: rejected answer PUT is terminal with its own reason.
    {
        FakeTransport transport;
        Recorder rec;
        VirtualClock clock;
        transport.responder = [&](const km::HttpRequest& r) -> km::HttpResponse {
            if (r.method == "POST") return {201, sessionBody(1000, 15000, 2000, 60000), "", {}};
            if (r.method == "GET") return {200, kOfferBodySmall, "", {}};
            return {500, "", "", {}};
        };
        SignalingWorker worker(transport, "https://example.test", rec.callbacks(), clock.source());
        CHECK(worker.start());
        CHECK(waitFinished(worker, std::chrono::seconds(5)));
        CHECK(!worker.cancel());
        CHECK(rec.phases == (std::vector<P>{P::CreatingSession, P::SessionCreated, P::WaitingForOffer,
                P::GeneratingAnswer, P::SendingAnswer, P::Failed}));
        CHECK(rec.details.back() == "answer send failed");
        CHECK(transport.count("PUT", "/answer") == 1);
    }
    // 6: cancel() interrupts a blocked offer poll on the real clock: prompt join,
    // single active run, idempotent afterwards, terminal Canceled phase.
    {
        FakeTransport transport;
        Recorder rec;
        transport.blockOffers = true;
        transport.responder = [](const km::HttpRequest& r) -> km::HttpResponse {
            if (r.method == "POST") return {201, sessionBody(50, 1000, 100, 10000), "", {}};
            return {0, "", "unexpected request", {}};
        };
        SignalingWorker worker(transport, "https://example.test", rec.callbacks());
        CHECK(worker.start());
        {
            std::unique_lock<std::mutex> lock(transport.mu);
            CHECK(transport.cv.wait_for(lock, std::chrono::seconds(5),
                    [&] { return transport.offerBlocked; }));
        }
        CHECK(worker.running());
        CHECK(!worker.start()); // one active run only
        const auto began = std::chrono::steady_clock::now();
        CHECK(worker.cancel());
        const auto tookMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - began).count();
        CHECK(tookMs < 2000);
        CHECK(!worker.cancel()); // idempotent after the join
        CHECK(!worker.running());
        CHECK(rec.phases == (std::vector<P>{P::CreatingSession, P::SessionCreated, P::WaitingForOffer, P::Canceled}));
        CHECK(rec.answers == 0);
        CHECK(transport.count("PUT", "/answer") == 0);
        CHECK(oneWorkerThread(rec.callbackThreads, transport.performThreads));
    }
    std::cout << "signaling_worker: session flow, backoff/timeout, failure phases, cancellation and "
                 "worker-thread-only blocking passed\n";
    return 0;
}
