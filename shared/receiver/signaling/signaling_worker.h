#pragma once
#include "session_client.h"
#include "../../km/receiver_contracts.h"
#include <atomic>
#include <functional>
#include <optional>
#include <string>
#include <thread>

namespace km::signaling {
// Lifecycle phase of one session attempt; stable values for UI mapping.
enum class SignalingPhase {
    CreatingSession,  // POST /v1/sessions in flight
    SessionCreated,   // session identity and joinUrl ready (also onSessionCreated)
    WaitingForOffer,  // polling GET offer with the session's interval/backoff/timeout
    GeneratingAnswer, // offer received; makeAnswer runs on the worker thread
    SendingAnswer,    // PUT answer in flight
    Succeeded,        // answer accepted (204) - terminal
    TimedOut,         // poll policy timeoutMs reached - terminal
    Failed,           // create/answer/send failed; detail carries the reason - terminal
    Canceled,         // cancel() interrupted the run - terminal
};

// Monotonic clock seam for polling deadlines. The default reads steady_clock and
// really sleeps in 20ms slices (the Windows worker's cadence); tests inject virtual
// time so backoff and timeout are deterministic. Only the worker thread uses it.
struct SignalingTimeSource {
    std::function<int64_t()> nowMonotonicNs;         // empty -> steady_clock
    std::function<void(int64_t durationNs)> sleepNs; // empty -> real sleep
};

// One session attempt on one dedicated thread, mirroring windows/receiver/app
// app_controller.cpp SignalingWorkerProc: create session, poll the offer with the
// server's interval/backoff/timeout policy, build the answer, PUT it. Every blocking
// IHttpTransport call happens on this thread only - never on the UI thread, RTC
// callbacks or the video worker. Callbacks run on the worker thread; the receiver
// marshals them (Windows posts to the UI dispatcher). Callbacks must not throw.
//
// Contract: start() and cancel() are called from one controlling thread, never from
// a callback. cancel() aborts an in-flight request via IHttpTransport::cancel() and
// joins; it is idempotent and returns true only when it stopped an active run. The
// transport is borrowed: it must outlive the worker and be used only by it while a
// run is active. makeAnswer is required; it may block (RTC answer generation runs
// here, as on Windows) and returning nullopt fails the run.
//
// Session deletion stays with the caller (SessionClient::DeleteSession); the Windows
// app flow likewise never deletes from the worker.
class SignalingWorker {
public:
    using AnswerFactory =
        std::function<std::optional<std::string>(const std::string& offerSdp, const CreateSessionResponse& session)>;

    struct Callbacks {
        std::function<void(const CreateSessionResponse&)> onSessionCreated; // joinUrl for QR/UI
        std::function<void(SignalingPhase phase, const std::string& detail)> onPhase;
        AnswerFactory makeAnswer; // required
    };

    // Throws std::invalid_argument for a bad base URL (SessionClient) or a missing
    // answer factory.
    // keepPollingAfterAnswer: after Succeeded keep polling offers until the session
    // timeout or cancel so a sender reconnect (a fresh offer on the same session)
    // is answered too. That late end reports no phase - the run already succeeded.
    SignalingWorker(km::IHttpTransport& transport, std::string baseUrl, Callbacks callbacks,
                    SignalingTimeSource time = {}, bool keepPollingAfterAnswer = false);
    ~SignalingWorker();

    SignalingWorker(const SignalingWorker&) = delete;
    SignalingWorker& operator=(const SignalingWorker&) = delete;

    // Spawn the worker thread. False while a run is active or if the thread could
    // not be created (a finished run is reaped here; an active one needs cancel()).
    bool start(std::string clientName = "receiver");
    // Interrupt and join. True only when an active run was canceled.
    bool cancel();
    bool running() const { return running_; }

private:
    void run(std::string clientName);
    void report(SignalingPhase phase, const std::string& detail = "");
    void fail(const std::string& error, const char* fallback);

    km::IHttpTransport& transport_;
    km::signaling::SessionClient client_;
    Callbacks callbacks_;
    SignalingTimeSource time_;
    bool keepPollingAfterAnswer_;
    std::thread thread_;
    std::atomic<bool> cancelRequested_{false};
    std::atomic<bool> running_{false};
};
} // namespace km::signaling
