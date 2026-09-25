// Offline answer wiring for the Mac RTC path (M4-a makeAnswer): the Signaling
// worker drives PeerConnectionManager::Initialize(session.rtcConfiguration) and
// ProcessOfferAndGenerateAnswer exactly as macos/host/app_delegate.mm wires them.
// The transport is synthetic, the offer carries no ICE candidates, and the
// answer scenario's configuration has no ICE servers, so gathering only
// collects this host's own candidates and no packet leaves the process. A
// connection is never attempted and never asserted: the Connected state and
// video frames must stay absent.
#include "peer_connection_manager.h"

#include "receiver/signaling/signaling_worker.h"

#include "km/json.h"

#include <cctype>
#include <chrono>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ":" << __LINE__ << ": " #x "\n"; return 1; } } while (false)

namespace {
using km::rtc_net::PeerConnectionManager;
using km::rtc_net::PeerState;
using km::signaling::CreateSessionResponse;
using km::signaling::SignalingPhase;
using km::signaling::SignalingWorker;
using P = SignalingPhase;

// Minimal WebRTC offer: one H264 video m-line with valid ICE credentials and
// fingerprint, no candidates (nothing is ever probed).
const std::string kOfferSdp =
    "v=0\r\n"
    "o=- 4611731400430053370 2 IN IP4 127.0.0.1\r\n"
    "s=-\r\n"
    "t=0 0\r\n"
    "a=group:BUNDLE 0\r\n"
    "a=msid-semantic: WMS\r\n"
    "m=video 9 UDP/TLS/RTP/SAVPF 96\r\n"
    "c=IN IP4 0.0.0.0\r\n"
    "a=rtcp:9 IN IP4 0.0.0.0\r\n"
    "a=ice-ufrag:8Wcn\r\n"
    "a=ice-pwd:0VqmdQffxmMVjgWWGjNiLZqr\r\n"
    "a=ice-options:trickle\r\n"
    "a=fingerprint:sha-256 6B:8B:5D:EA:59:04:20:23:29:C8:87:1C:CC:87:32:BE:DD:8C:66:A5:8E:50:55:"
    "EA:8C:D3:B6:5C:29:82:1D:50\r\n"
    "a=setup:actpass\r\n"
    "a=mid:0\r\n"
    "a=sendrecv\r\n"
    "a=rtcp-mux\r\n"
    "a=rtcp-rsize\r\n"
    "a=rtpmap:96 H264/90000\r\n"
    "a=fmtp:96 level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f\r\n";

// Browser-shaped offer for the same path: the structure the web sender produces
// in MediaTrack mode (video with H264 plus non-negotiated codecs and RTX, an
// audio m-line this build may reject), constructed here rather than captured.
// It reproduces the codec filtering the app must survive on a real offer.
const std::string kBrowserOfferSdp =
    "v=0\r\n"
    "o=- 4611731400430053370 2 IN IP4 127.0.0.1\r\n"
    "s=-\r\n"
    "t=0 0\r\n"
    "a=group:BUNDLE 0 1\r\n"
    "a=extmap-allow-mixed\r\n"
    "a=msid-semantic: WMS\r\n"
    "m=video 9 UDP/TLS/RTP/SAVPF 96 97 98 99 100 101 102 103\r\n"
    "c=IN IP4 0.0.0.0\r\n"
    "a=rtcp:9 IN IP4 0.0.0.0\r\n"
    "a=ice-ufrag:8Wcn\r\n"
    "a=ice-pwd:0VqmdQffxmMVjgWWGjNiLZqr\r\n"
    "a=ice-options:trickle\r\n"
    "a=fingerprint:sha-256 6B:8B:5D:EA:59:04:20:23:29:C8:87:1C:CC:87:32:BE:DD:8C:66:A5:8E:50:55:"
    "EA:8C:D3:B6:5C:29:82:1D:50\r\n"
    "a=setup:actpass\r\n"
    "a=mid:0\r\n"
    "a=sendrecv\r\n"
    "a=extmap:1 urn:ietf:params:rtp-hdrext:toffset\r\n"
    "a=extmap:2 http://www.webrtc.org/experiments/rtp-hdrext/abs-send-time\r\n"
    "a=extmap:3 urn:ietf:params:rtp-hdrext:sdes:transport-wide-cc-requests\r\n"
    "a=extmap:4 urn:ietf:params:rtp-hdrext:sdes:rtp-stream-id\r\n"
    "a=extmap:5 urn:ietf:params:rtp-hdrext:sdes:repaired-rtp-stream-id\r\n"
    "a=extmap:6 http://www.webrtc.org/experiments/rtp-hdrext/playout-delay\r\n"
    "a=extmap:7 http://www.webrtc.org/experiments/rtp-hdrext/video-content-type\r\n"
    "a=extmap:8 http://www.webrtc.org/experiments/rtp-hdrext/video-timing\r\n"
    "a=extmap:10 http://www.webrtc.org/experiments/rtp-hdrext/color-space\r\n"
    "a=extmap:11 urn:ietf:params:rtp-hdrext:des:dependency-descriptor-00\r\n"
    "a=rtcp-mux\r\n"
    "a=rtcp-rsize\r\n"
    "a=rtpmap:96 VP8/90000\r\n"
    "a=rtpmap:97 rtx/90000\r\n"
    "a=fmtp:97 apt=96\r\n"
    "a=rtpmap:98 H264/90000\r\n"
    "a=fmtp:98 level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f\r\n"
    "a=rtpmap:99 AV1/90000\r\n"
    "a=fmtp:99 level-idx=0;profile=0;level=0\r\n"
    "a=rtpmap:100 VP9/90000\r\n"
    "a=fmtp:100 level-idx=0;profile-id=0\r\n"
    "a=rtpmap:101 H265/90000\r\n"
    "a=fmtp:101 level-asymmetry-allowed=1;level-id=93;profile-id=1\r\n"
    "a=rtpmap:102 red/90000\r\n"
    "a=rtpmap:103 ulpfec/90000\r\n"
    "a=fmtp:103 red=102\r\n"
    "a=rtcp-fb:96 goog-remb\r\n"
    "a=rtcp-fb:96 transport-cc\r\n"
    "a=rtcp-fb:96 ccm fir\r\n"
    "a=rtcp-fb:96 nack\r\n"
    "a=rtcp-fb:96 nack pli\r\n"
    "a=ssrc-group:FID 1001 1002\r\n"
    "a=msid:kmstream videotrack0\r\n"
    "a=ssrc:1001 msid:kmstream videotrack0\r\n"
    "a=ssrc:1002 msid:kmstream videoretrack0\r\n"
    "m=audio 9 UDP/TLS/RTP/SAVPF 111 63 9 0 8 103 13\r\n"
    "c=IN IP4 0.0.0.0\r\n"
    "a=rtcp:9 IN IP4 0.0.0.0\r\n"
    "a=ice-ufrag:8Wcn\r\n"
    "a=ice-pwd:0VqmdQffxmMVjgWWGjNiLZqr\r\n"
    "a=ice-options:trickle\r\n"
    "a=fingerprint:sha-256 6B:8B:5D:EA:59:04:20:23:29:C8:87:1C:CC:87:32:BE:DD:8C:66:A5:8E:50:55:"
    "EA:8C:D3:B6:5C:29:82:1D:50\r\n"
    "a=setup:actpass\r\n"
    "a=mid:1\r\n"
    "a=sendrecv\r\n"
    "a=extmap:14 urn:ietf:params:rtp-hdrext:ssrc-audio-level\r\n"
    "a=extmap:2 http://www.webrtc.org/experiments/rtp-hdrext/abs-send-time\r\n"
    "a=extmap:4 urn:ietf:params:rtp-hdrext:sdes:rtp-stream-id\r\n"
    "a=rtcp-mux\r\n"
    "a=rtcp-rsize\r\n"
    "a=rtpmap:111 opus/48000/2\r\n"
    "a=fmtp:111 minptime=10;useinbandfec=1\r\n"
    "a=rtpmap:63 red/48000/2\r\n"
    "a=rtpmap:9 G722/8000\r\n"
    "a=rtpmap:0 PCMU/8000\r\n"
    "a=rtpmap:8 PCMA/8000\r\n"
    "a=rtpmap:103 CN/8000\r\n"
    "a=rtpmap:13 telephone-event/8000\r\n"
    "a=msid:kmstream audiotrack0\r\n"
    "a=ssrc:2001 msid:kmstream audiotrack0\r\n";

const std::string kOfferBody =
    std::string(R"({"type":"offer","sdp":)") + km::json::quote(kOfferSdp) + "}";

// Poll values satisfy session_codec.h's validation (initial >= 20, max >= initial,
// timeout >= max, backoff <= timeout). The offer is available on the first poll.
std::string sessionBody(const std::string& rtcConfigurationJson) {
    return std::string(R"({"sessionId":"sess-rtc","receiverToken":"tok-rtc",)") +
        R"("joinUrl":"http://127.0.0.1:8787/send/sess-rtc",)" +
        R"("expiresAt":"2026-09-25T00:00:00Z","poll":{)" +
        R"("initialIntervalMs":20,"backoffAfterMs":1000,"maxIntervalMs":100,)" +
        R"("timeoutMs":10000},"rtcConfiguration":)" + rtcConfigurationJson + "}";
}

const std::string kConfigAllEmpty =
    R"({"iceTransportPolicy":"all","iceServers":[]})";
const std::string kConfigRelayTurnUdp =
    R"({"iceTransportPolicy":"relay",)"
    R"("iceServers":[{"urls":["turn:127.0.0.1:3478"],"username":"u","credential":"p"}]})";
const std::string kConfigRelayTurnTls =
    R"({"iceTransportPolicy":"relay",)"
    R"("iceServers":[{"urls":["turns:127.0.0.1:5349"],"username":"u","credential":"p"}]})";

// Synthetic transport: create session, immediate offer, captured answer.
struct FakeTransport : km::IHttpTransport {
    mutable std::mutex mu;
    std::vector<km::HttpRequest> requests;
    std::string sessionJson;
    std::string offerBody = kOfferBody;
    int offerPolls = 0;

    km::HttpResponse perform(const km::HttpRequest& r) override {
        {
            std::lock_guard<std::mutex> lock(mu);
            requests.push_back(r);
        }
        if (r.method == "POST" && r.url.ends_with("/v1/sessions"))
            return {201, sessionJson, "", {}};
        if (r.method == "GET" && r.url.ends_with("/offer")) {
            ++offerPolls;
            return {200, offerBody, "", {}};
        }
        if (r.method == "PUT" && r.url.ends_with("/answer")) return {204, "", "", {}};
        return {0, "", "unexpected request", {}};
    }
    void cancel() override {}
    std::string answerBody() const {
        std::lock_guard<std::mutex> lock(mu);
        for (const auto& r : requests)
            if (r.method == "PUT" && r.url.ends_with("/answer")) return r.body;
        return {};
    }
    int count(const std::string& method, const std::string& suffix) const {
        std::lock_guard<std::mutex> lock(mu);
        int n = 0;
        for (const auto& r : requests)
            if (r.method == method && r.url.ends_with(suffix)) ++n;
        return n;
    }
};

bool waitFinished(SignalingWorker& worker, std::chrono::milliseconds limit) {
    const auto until = std::chrono::steady_clock::now() + limit;
    while (worker.running() && std::chrono::steady_clock::now() < until)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return !worker.running();
}

void dumpPhases(const std::vector<P>& phases, const std::vector<std::string>& details) {
    for (size_t i = 0; i < phases.size(); ++i)
        std::cerr << "  phase[" << i << "]=" << int(phases[i])
                  << " detail=" << (i < details.size() ? details[i] : "") << "\n";
}
} // namespace

int main() {
    // 1: answer generation through the app's makeAnswer path. The session has no
    // ICE servers, so gathering stays local; the answer must carry the local
    // answerer description for the offered H264 video.
    {
        FakeTransport transport;
        transport.sessionJson = sessionBody(kConfigAllEmpty);
        PeerConnectionManager mgr;
        std::vector<P> phases;
        std::vector<std::string> details;
        std::vector<PeerState> states;
        int videoFrames = 0;
        SignalingWorker::Callbacks cb;
        cb.onPhase = [&](P phase, const std::string& detail) {
            phases.push_back(phase);
            details.push_back(detail);
        };
        cb.onSessionCreated = [](const CreateSessionResponse&) {};
        cb.makeAnswer = [&](const std::string& offerSdp,
                            const CreateSessionResponse& session) -> std::optional<std::string> {
            // Mirror of app_delegate.mm: the RTC configuration comes from the
            // session response and answer generation runs on the worker thread.
            if (!mgr.Initialize(session.rtcConfiguration,
                    [&](PeerState state) { states.push_back(state); },
                    [&](const uint8_t*, size_t, int, int, int64_t) { ++videoFrames; },
                    [](const int16_t*, size_t, int, int) {}))
                return std::nullopt;
            std::string answer;
            if (!mgr.ProcessOfferAndGenerateAnswer(offerSdp, answer)) return std::nullopt;
            return answer;
        };
        SignalingWorker worker(transport, "http://127.0.0.1:8787", cb);
        CHECK(worker.start("rtc-answer-test"));
        const bool finished = waitFinished(worker, std::chrono::seconds(30));
        CHECK(worker.cancel() == false); // completed on its own; cancel only reaps
        if (!finished || phases.size() != 6) dumpPhases(phases, details);
        CHECK(finished);
        CHECK(phases == (std::vector<P>{P::CreatingSession, P::SessionCreated, P::WaitingForOffer,
                P::GeneratingAnswer, P::SendingAnswer, P::Succeeded}));
        CHECK(transport.count("PUT", "/answer") == 1);
        mgr.Close(); // control thread (test main) after the worker joined
        const std::string body = transport.answerBody();
        CHECK(!body.empty());
        const auto parsed = km::json::parse(body);
        CHECK(parsed.at("type").string() == "answer");
        const std::string sdp = parsed.at("sdp").string();
        CHECK(sdp.starts_with("v=0"));
        CHECK(sdp.find("m=video") != std::string::npos);
        CHECK(sdp.find("a=mid:0") != std::string::npos);
        CHECK(sdp.find("H264") != std::string::npos); // the offered codec survives
        CHECK(sdp.find("a=fingerprint:sha-256") != std::string::npos);
        CHECK(sdp.find("a=setup:") != std::string::npos);
        CHECK(sdp.find("a=ice-ufrag:") != std::string::npos);
        CHECK(sdp != kOfferSdp);
        // No connection: nothing is sent or received, and Connected never fires.
        CHECK(videoFrames == 0);
        for (PeerState state : states) CHECK(state != PeerState::Connected);
    }
    // 2: TURN carryover. The session's relay policy + UDP TURN settings reach
    // Initialize and pass the same gate rtc_load checks. Initialize only
    // constructs the PeerConnection (no gathering, no packets); the canned
    // answer keeps this scenario free of any network use - answer generation
    // itself is scenario 1's job.
    {
        FakeTransport transport;
        transport.sessionJson = sessionBody(kConfigRelayTurnUdp);
        PeerConnectionManager mgr;
        std::vector<P> phases;
        std::vector<std::string> details;
        bool initialized = false;
        SignalingWorker::Callbacks cb;
        cb.onPhase = [&](P phase, const std::string& detail) {
            phases.push_back(phase);
            details.push_back(detail);
        };
        cb.onSessionCreated = [](const CreateSessionResponse&) {};
        cb.makeAnswer = [&](const std::string&, const CreateSessionResponse& session)
            -> std::optional<std::string> {
            initialized = mgr.Initialize(session.rtcConfiguration,
                [](PeerState) {},
                [](const uint8_t*, size_t, int, int, int64_t) {},
                [](const int16_t*, size_t, int, int) {});
            if (!initialized) return std::nullopt;
            return std::string("v=0\r\ns=-\r\nt=0 0\r\nm=video 9 UDP/TLS/RTP/SAVPF 96\r\n");
        };
        SignalingWorker worker(transport, "http://127.0.0.1:8787", cb);
        CHECK(worker.start("rtc-answer-test"));
        CHECK(waitFinished(worker, std::chrono::seconds(30)));
        CHECK(!worker.cancel());
        CHECK(phases == (std::vector<P>{P::CreatingSession, P::SessionCreated, P::WaitingForOffer,
                P::GeneratingAnswer, P::SendingAnswer, P::Succeeded}));
        CHECK(initialized);
        CHECK(transport.count("PUT", "/answer") == 1);
        mgr.Close();
    }
    // 3: a TURN-over-TLS endpoint is rejected through the same session path
    // (KM_TURN_TCP_TLS=0 skips it, relay-only is then unusable), so the run
    // fails explicitly instead of pretending the relay will work.
    {
        FakeTransport transport;
        transport.sessionJson = sessionBody(kConfigRelayTurnTls);
        PeerConnectionManager mgr;
        std::vector<P> phases;
        std::vector<std::string> details;
        SignalingWorker::Callbacks cb;
        cb.onPhase = [&](P phase, const std::string& detail) {
            phases.push_back(phase);
            details.push_back(detail);
        };
        cb.onSessionCreated = [](const CreateSessionResponse&) {};
        cb.makeAnswer = [&](const std::string&, const CreateSessionResponse& session)
            -> std::optional<std::string> {
            if (!mgr.Initialize(session.rtcConfiguration,
                    [](PeerState) {},
                    [](const uint8_t*, size_t, int, int, int64_t) {},
                    [](const int16_t*, size_t, int, int) {}))
                return std::nullopt;
            return std::nullopt; // unreachable: Initialize fails first
        };
        SignalingWorker worker(transport, "http://127.0.0.1:8787", cb);
        CHECK(worker.start("rtc-answer-test"));
        CHECK(waitFinished(worker, std::chrono::seconds(30)));
        CHECK(!worker.cancel());
        CHECK(phases == (std::vector<P>{P::CreatingSession, P::SessionCreated, P::WaitingForOffer,
                P::GeneratingAnswer, P::Failed}));
        CHECK(details.size() == phases.size());
        CHECK(details.back() == "answer generation failed");
        CHECK(transport.count("PUT", "/answer") == 0);
        mgr.Close();
    }
    // 4: the browser-shaped offer (multi-codec video with RTX plus an audio
    // m-line) through the same makeAnswer path: codec filtering must keep H264,
    // drop what this build cannot answer with, and still produce an answer.
    {
        FakeTransport transport;
        transport.sessionJson = sessionBody(kConfigAllEmpty);
        transport.offerBody =
            std::string(R"({"type":"offer","sdp":)") + km::json::quote(kBrowserOfferSdp) + "}";
        PeerConnectionManager mgr;
        std::vector<P> phases;
        std::vector<std::string> details;
        std::vector<PeerState> states;
        int videoFrames = 0;
        SignalingWorker::Callbacks cb;
        cb.onPhase = [&](P phase, const std::string& detail) {
            phases.push_back(phase);
            details.push_back(detail);
        };
        cb.onSessionCreated = [](const CreateSessionResponse&) {};
        cb.makeAnswer = [&](const std::string& offerSdp,
                            const CreateSessionResponse& session) -> std::optional<std::string> {
            if (!mgr.Initialize(session.rtcConfiguration,
                    [&](PeerState state) { states.push_back(state); },
                    [&](const uint8_t*, size_t, int, int, int64_t) { ++videoFrames; },
                    [](const int16_t*, size_t, int, int) {}))
                return std::nullopt;
            std::string answer;
            if (!mgr.ProcessOfferAndGenerateAnswer(offerSdp, answer)) return std::nullopt;
            return answer;
        };
        SignalingWorker worker(transport, "http://127.0.0.1:8787", cb);
        CHECK(worker.start("rtc-answer-test"));
        const bool finished = waitFinished(worker, std::chrono::seconds(30));
        CHECK(worker.cancel() == false);
        if (!finished || phases.size() != 6) dumpPhases(phases, details);
        CHECK(finished);
        CHECK(phases == (std::vector<P>{P::CreatingSession, P::SessionCreated, P::WaitingForOffer,
                P::GeneratingAnswer, P::SendingAnswer, P::Succeeded}));
        CHECK(transport.count("PUT", "/answer") == 1);
        mgr.Close();
        const auto parsed = km::json::parse(transport.answerBody());
        CHECK(parsed.at("type").string() == "answer");
        const std::string sdp = parsed.at("sdp").string();
        CHECK(sdp.find("m=video") != std::string::npos);
        CHECK(sdp.find("H264") != std::string::npos); // the only codec we answer with
        // non-negotiated codecs never appear in the answer
        CHECK(sdp.find("VP8") == std::string::npos);
        CHECK(sdp.find("VP9") == std::string::npos);
        CHECK(sdp.find("AV1") == std::string::npos);
        CHECK(sdp.find("H265") == std::string::npos);
        // audio is either rejected (port 0 / omitted) or answered with opus
        const auto audioPos = sdp.find("m=audio");
        if (audioPos != std::string::npos) {
            if (sdp.compare(audioPos, 9, "m=audio 0") == 0) {
                // The rejected line must keep payload types: browsers reject the
                // whole answer when the format list after the protocol is empty.
                const auto proto = sdp.find("SAVPF ", audioPos);
                CHECK(proto != std::string::npos);
                CHECK(proto + 6 < sdp.size());
                CHECK(std::isdigit(static_cast<unsigned char>(sdp[proto + 6])));
            } else {
                CHECK(sdp.find("opus") != std::string::npos);
            }
        }
        CHECK(videoFrames == 0);
        for (PeerState state : states) CHECK(state != PeerState::Connected);
    }
    std::cout << "rtc_answer ok\n";
    return 0;
}
