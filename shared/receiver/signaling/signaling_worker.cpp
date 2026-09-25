#include "signaling_worker.h"
#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <utility>

namespace km::signaling {
namespace {
SignalingTimeSource withDefaults(SignalingTimeSource time) {
    if (!time.nowMonotonicNs)
        time.nowMonotonicNs = [] {
            return std::chrono::duration_cast<std::chrono::nanoseconds>(
                       std::chrono::steady_clock::now().time_since_epoch())
                .count();
        };
    if (!time.sleepNs)
        time.sleepNs = [](int64_t durationNs) {
            std::this_thread::sleep_for(std::chrono::nanoseconds(durationNs));
        };
    return time;
}
} // namespace

SignalingWorker::SignalingWorker(km::IHttpTransport& transport, std::string baseUrl, Callbacks callbacks,
                                 SignalingTimeSource time, bool keepPollingAfterAnswer)
    : transport_(transport), client_(transport_, std::move(baseUrl)), callbacks_(std::move(callbacks)),
      keepPollingAfterAnswer_(keepPollingAfterAnswer) {
    if (!callbacks_.makeAnswer) throw std::invalid_argument("missing answer handler");
    time_ = withDefaults(std::move(time));
}

SignalingWorker::~SignalingWorker() { (void)cancel(); }

bool SignalingWorker::start(std::string clientName) {
    if (running_) return false;
    if (thread_.joinable()) thread_.join(); // reap a finished run before reuse
    cancelRequested_ = false;
    running_ = true;
    try {
        thread_ = std::thread(&SignalingWorker::run, this, std::move(clientName));
    } catch (...) {
        running_ = false;
        return false;
    }
    return true;
}

bool SignalingWorker::cancel() {
    if (!thread_.joinable()) return false;
    const bool wasActive = running_.load();
    cancelRequested_ = true;
    if (wasActive) transport_.cancel(); // aborts an in-flight perform on the worker
    thread_.join();
    return wasActive;
}

void SignalingWorker::run(std::string clientName) {
    struct RunDone { // clears running_ on every exit path, after the last report
        SignalingWorker* worker;
        ~RunDone() { worker->running_ = false; }
    } done{this};

    if (cancelRequested_) { report(SignalingPhase::Canceled); return; }
    report(SignalingPhase::CreatingSession);
    const auto session = client_.CreateSession(std::move(clientName));
    if (!session) { fail(client_.LastError(), "session creation failed"); return; }
    if (cancelRequested_) { report(SignalingPhase::Canceled); return; }
    report(SignalingPhase::SessionCreated);
    if (callbacks_.onSessionCreated) callbacks_.onSessionCreated(*session);
    report(SignalingPhase::WaitingForOffer);

    const int64_t begin = time_.nowMonotonicNs();
    int64_t intervalMs = session->poll.initialIntervalMs;
    bool answered = false;
    std::string answeredOfferSdp;
    while (!cancelRequested_) {
        const int64_t elapsedMs = (time_.nowMonotonicNs() - begin) / 1'000'000;
        if (elapsedMs >= int64_t(session->poll.timeoutMs)) {
            // A completed run ends quietly on session age-out: the success phases
            // stay reported, only the pending reconnect wait stops.
            if (!answered) report(SignalingPhase::TimedOut);
            return;
        }
        auto offer = client_.PollOffer(session->sessionId, session->receiverToken);
        // The server keeps the stored offer readable after an answer, so the
        // reconnect wait would see the answered offer on every poll. Answering it
        // again from a fresh PeerConnection produces a DIFFERENT answer SDP that
        // the server rejects (409 conflicting answer). Only a genuinely new offer
        // - a sender reconnect stores a fresh one and resets the answer - gets an
        // answer; the identical one is waited out.
        if (offer && !cancelRequested_ && offer->sdp != answeredOfferSdp) {
            report(SignalingPhase::GeneratingAnswer);
            const auto answer = callbacks_.makeAnswer(offer->sdp, *session);
            if (cancelRequested_) { report(SignalingPhase::Canceled); return; }
            if (!answer || answer->empty()) {
                report(SignalingPhase::Failed, "answer generation failed");
                return;
            }
            report(SignalingPhase::SendingAnswer);
            if (!client_.PutAnswer(session->sessionId, session->receiverToken, *answer)) {
                fail(client_.LastError(), "answer send failed");
                return;
            }
            report(SignalingPhase::Succeeded);
            answeredOfferSdp = offer->sdp;
            answered = true;
            if (!keepPollingAfterAnswer_) return;
        }
        if (cancelRequested_) break;
        // Windows cadence: fixed +500ms step once backoffAfterMs passed, clamped to max.
        if (elapsedMs > int64_t(session->poll.backoffAfterMs))
            intervalMs = std::min(intervalMs + 500, int64_t(session->poll.maxIntervalMs));
        for (int64_t slept = 0; slept < intervalMs && !cancelRequested_; slept += 20)
            time_.sleepNs(20'000'000);
    }
    report(SignalingPhase::Canceled);
}

void SignalingWorker::report(SignalingPhase phase, const std::string& detail) {
    if (callbacks_.onPhase) callbacks_.onPhase(phase, detail);
}

void SignalingWorker::fail(const std::string& error, const char* fallback) {
    if (cancelRequested_) { report(SignalingPhase::Canceled); return; }
    report(SignalingPhase::Failed, error.empty() ? fallback : error);
}

} // namespace km::signaling
