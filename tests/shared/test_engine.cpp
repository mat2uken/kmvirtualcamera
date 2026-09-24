#include "receiver/engine/receiver_engine.h"
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <random>
#include <string>
#include <vector>
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ":" << __LINE__ << ": " #x "\n"; return 1; } } while (false)
using Bytes = std::vector<uint8_t>;
using km::engine::KeyframeChannel;
using km::engine::MediaPath;

static Bytes rtp(uint16_t seq, uint32_t ts, bool marker, const Bytes& payload, uint8_t pt = 96, uint32_t ssrc = 1) {
    Bytes out{0x80, uint8_t(pt | (marker ? 128 : 0)), uint8_t(seq >> 8), uint8_t(seq),
        uint8_t(ts >> 24), uint8_t(ts >> 16), uint8_t(ts >> 8), uint8_t(ts),
        uint8_t(ssrc >> 24), uint8_t(ssrc >> 16), uint8_t(ssrc >> 8), uint8_t(ssrc)};
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}
static Bytes dcPacket(uint16_t seq, uint32_t ts, bool key, uint8_t index, uint8_t count, const Bytes& data) {
    Bytes p{0x4d, 0x4b, uint8_t((key ? 1 : 0) | (index == 0 ? 4 : 0) | (index + 1 == count ? 8 : 0)),
        1, index, count, uint8_t(seq), uint8_t(seq >> 8),
        uint8_t(ts), uint8_t(ts >> 8), uint8_t(ts >> 16), uint8_t(ts >> 24)};
    p.insert(p.end(), data.begin(), data.end());
    return p;
}
struct Harness {
    km::engine::ReceiverEngine engine;
    std::vector<km::EncodedVideoFrame> frames;
    std::vector<KeyframeChannel> keyframeChannels;
    std::vector<std::string> controls;
    int lossCalls = 0;
    uint64_t lastLossNs = 0;
    Harness() {
        engine.setFrameHandler([this](const km::EncodedVideoFrame& f) { frames.push_back(f); });
        engine.setKeyframeRequestHandler([this](KeyframeChannel c) { keyframeChannels.push_back(c); });
        engine.setControlSendHandler([this](const std::string& text) { controls.push_back(text); });
        engine.setLossHandler([this](uint64_t nowNs) { ++lossCalls; lastLossNs = nowNs; });
    }
};

int main() {
    const Bytes config{0x78, 0, 3, 0x67, 0x42, 0x80, 0, 2, 0x68, 0xce};
    const Bytes idr{0x65, 1};
    const Bytes pframe{0x41, 4};
    const Bytes key{0, 0, 0, 1, 0x67, 0x42, 0x80, 0, 0, 1, 0x68, 0xce, 0, 0, 1, 0x65, 0xaa};
    const Bytes delta{0, 0, 1, 0x41, 0xbb};
    // 1: RTP happy path - owning AU, generation, arrival stamp, 90kHz domain.
    {
        Harness h;
        h.engine.setVideoPayloadTypes({96});
        h.engine.setGeneration(7);
        const Bytes a = rtp(0, 0, false, config), b = rtp(1, 0, true, idr);
        h.engine.processRtpVideoPacket(a.data(), a.size(), 1'000'000);
        h.engine.processRtpVideoPacket(b.data(), b.size(), 2'000'000);
        CHECK(h.frames.size() == 1);
        CHECK(h.frames[0].randomAccess);
        CHECK(h.frames[0].timestampDomain == km::TimestampDomain::Rtp90kHz);
        CHECK(h.frames[0].mediaTicks == 0);
        CHECK(h.frames[0].receivedMonotonicNs == 2'000'000);
        CHECK(h.frames[0].generation == 7);
        CHECK(!h.frames[0].annexB.empty());
        const Bytes p = rtp(2, 3000, true, pframe);
        h.engine.processRtpVideoPacket(p.data(), p.size(), 3'000'000);
        CHECK(h.frames.size() == 2);
        CHECK(h.frames[1].mediaTicks == 3000);
        CHECK(!h.frames[1].randomAccess);
        CHECK(h.frames[1].receivedMonotonicNs == 3'000'000);
        CHECK(h.engine.stats().rtpPackets == 3);
        CHECK(h.engine.stats().rtpPacketsDropped == 0);
        CHECK(h.engine.stats().rtpFrames == 2);
        CHECK(h.engine.stats().lastFrameArrivalNs == 3'000'000);
        CHECK(h.engine.activePath() == MediaPath::Rtp);
        CHECK(!h.engine.needsKeyframe());
        CHECK(h.keyframeChannels.empty());
        CHECK(h.controls.empty());
    }
    // 2: payload-type filter, fail-closed.
    {
        Harness h; // no payload types configured
        const Bytes a = rtp(0, 0, false, config), b = rtp(1, 0, true, idr);
        h.engine.processRtpVideoPacket(a.data(), a.size(), 1'000);
        h.engine.processRtpVideoPacket(b.data(), b.size(), 2'000);
        CHECK(h.frames.empty());
        CHECK(h.engine.stats().rtpPackets == 2);
        CHECK(h.engine.stats().rtpPacketsDropped == 2);
        const uint8_t junk[] = {1, 2, 3};
        h.engine.processRtpVideoPacket(junk, sizeof junk, 3'000);
        h.engine.processRtpVideoPacket(nullptr, 0, 4'000);
        CHECK(h.engine.stats().rtpPacketsDropped == 4);
        h.engine.setVideoPayloadTypes({96});
        const Bytes wrong = rtp(2, 0, true, idr, 97);
        h.engine.processRtpVideoPacket(wrong.data(), wrong.size(), 5'000);
        CHECK(h.engine.stats().rtpPacketsDropped == 5);
        CHECK(h.frames.empty());
        const Bytes c2 = rtp(3, 6000, false, config), i2 = rtp(4, 6000, true, idr);
        h.engine.processRtpVideoPacket(c2.data(), c2.size(), 6'000);
        h.engine.processRtpVideoPacket(i2.data(), i2.size(), 7'000);
        CHECK(h.frames.size() == 1);
        CHECK(h.frames[0].randomAccess);
        CHECK(h.engine.stats().rtpPackets == 7);
        CHECK(h.engine.stats().rtpPacketsDropped == 5);
    }
    // 3: an SSRC change resets the 90kHz unwrap domain.
    {
        Harness h;
        h.engine.setVideoPayloadTypes({96});
        auto feed = [&](uint16_t seq, uint32_t ts, bool marker, const Bytes& payload, uint32_t ssrc, uint64_t at) {
            const Bytes pkt = rtp(seq, ts, marker, payload, 96, ssrc);
            h.engine.processRtpVideoPacket(pkt.data(), pkt.size(), at);
        };
        feed(0, 0x7F000000, false, config, 1, 1'000'000);
        feed(1, 0x7F000000, true, idr, 1, 2'000'000);
        CHECK(h.frames.size() == 1);
        CHECK(h.frames[0].mediaTicks == 0x7F000000);
        feed(2, 0x7F001000, true, pframe, 1, 3'000'000);
        CHECK(h.frames.size() == 2);
        CHECK(h.frames[1].mediaTicks == 0x7F001000); // unwrap continues within one SSRC
        feed(3, 0x100, false, config, 2, 4'000'000); // new SSRC
        feed(4, 0x100, true, idr, 2, 5'000'000);
        CHECK(h.frames.size() == 3);
        CHECK(h.frames[2].mediaTicks == 0x100); // fresh unwrap, not 0x7F001000 + delta
        CHECK(h.frames[2].timestampDomain == km::TimestampDomain::Rtp90kHz);
        CHECK(h.engine.stats().rtpFrames == 3);
        CHECK(h.engine.stats().rtpPacketsDropped == 0);
    }
    // 4: the DC path wins, close restores RTP, keyframe routing follows the channel.
    {
        Harness h;
        h.engine.setVideoPayloadTypes({96});
        h.engine.setGeneration(3);
        const Bytes a = rtp(0, 0xFFFFFF00, false, config), b = rtp(1, 0xFFFFFF00, true, idr);
        h.engine.processRtpVideoPacket(a.data(), a.size(), 1'000'000);
        h.engine.processRtpVideoPacket(b.data(), b.size(), 2'000'000);
        CHECK(h.frames.size() == 1);
        CHECK(h.frames[0].mediaTicks == int64_t{0xFFFFFF00});
        h.engine.onVideoDataChannelOpened();
        h.engine.requestKeyframe(3'000'000);
        CHECK(h.keyframeChannels.size() == 1);
        CHECK(h.keyframeChannels[0] == KeyframeChannel::DataChannel); // open channel routes to control
        const Bytes k0(key.begin(), key.begin() + 8), k1(key.begin() + 8, key.end());
        const Bytes dk0 = dcPacket(0, 1000, true, 0, 2, k0), dk1 = dcPacket(0, 1000, true, 1, 2, k1);
        h.engine.processDataChannelPacket(dk0.data(), dk0.size(), 4'000'000);
        CHECK(h.frames.size() == 1); // the chunked AU is not complete yet
        h.engine.processDataChannelPacket(dk1.data(), dk1.size(), 4'100'000);
        CHECK(h.frames.size() == 2);
        CHECK(h.engine.activePath() == MediaPath::DataChannel);
        CHECK(h.frames[1].timestampDomain == km::TimestampDomain::SenderMicroseconds);
        CHECK(h.frames[1].mediaTicks == 1000);
        CHECK(h.frames[1].randomAccess);
        // RTP is gated while the DC path is active.
        const Bytes c2 = rtp(2, 0x200, false, config), i2 = rtp(3, 0x200, true, idr);
        h.engine.processRtpVideoPacket(c2.data(), c2.size(), 6'000'000);
        h.engine.processRtpVideoPacket(i2.data(), i2.size(), 7'000'000);
        CHECK(h.frames.size() == 2);
        CHECK(h.engine.stats().rtpFrames == 1);
        CHECK(h.engine.stats().rtpPacketsDropped == 2);
        const Bytes dd = dcPacket(1, 1100, false, 0, 1, delta);
        h.engine.processDataChannelPacket(dd.data(), dd.size(), 8'000'000);
        CHECK(h.frames.size() == 3);
        CHECK(h.frames[2].mediaTicks == 1100);
        CHECK(!h.frames[2].randomAccess);
        h.engine.onDataChannelVideoClosed();
        CHECK(h.engine.activePath() == MediaPath::Rtp);
        CHECK(h.engine.needsKeyframe());
        h.engine.requestKeyframe(60'000'000); // 57ms after the request at 3ms
        CHECK(h.keyframeChannels.size() == 2);
        CHECK(h.keyframeChannels[1] == KeyframeChannel::Rtcp);
        // RTP resumes with a fresh unwrap domain and a re-armed recovery.
        const Bytes c3 = rtp(4, 0x100, false, config), i3 = rtp(5, 0x100, true, idr);
        h.engine.processRtpVideoPacket(c3.data(), c3.size(), 61'000'000);
        h.engine.processRtpVideoPacket(i3.data(), i3.size(), 62'000'000);
        CHECK(h.frames.size() == 4);
        CHECK(h.frames[3].mediaTicks == 0x100); // the close reset the unwrapper
        CHECK(!h.engine.needsKeyframe());
        // A non-key DC AU after the close cannot resume the DC path.
        const Bytes dn = dcPacket(2, 5, false, 0, 1, delta);
        h.engine.processDataChannelPacket(dn.data(), dn.size(), 63'000'000);
        CHECK(h.frames.size() == 4);
        CHECK(h.engine.activePath() == MediaPath::Rtp);
        const auto& s = h.engine.stats();
        CHECK(s.rtpPackets == 6);
        CHECK(s.rtpPacketsDropped == 2);
        CHECK(s.rtpFrames == 2);
        CHECK(s.dataChannelPackets == 4);
        CHECK(s.dataChannelFrames == 2);
        CHECK(s.keyframeRequestsDispatched == 2);
    }
    // 5: keyframe policy - 50ms throttle, timer retry, loss re-arm, configured-IDR clear.
    {
        Harness h;
        h.engine.setVideoPayloadTypes({96});
        h.engine.requestKeyframe(1'000'000'000); // first dispatch
        h.engine.requestKeyframe(1'010'000'000); // inside the window
        h.engine.requestKeyframe(1'050'000'000); // exactly 50ms later
        h.engine.requestKeyframe(1'060'000'000); // inside the window
        CHECK(h.engine.stats().keyframeRequestsDispatched == 2);
        CHECK(h.engine.needsKeyframe());
        h.engine.onTimer(1'200'000'000); // timer retry while armed
        CHECK(h.engine.stats().keyframeRequestsDispatched == 3);
        h.engine.onTimer(1'210'000'000);
        CHECK(h.engine.stats().keyframeRequestsDispatched == 3);
        CHECK(h.controls.size() == 2); // the idle DC timer emits PLI + bitrate once each
        CHECK(h.controls[0] == "{\"type\":\"pli\"}");
        CHECK(h.controls[1].find("{\"type\":\"bitrate\",\"bps\":") == 0);
        const Bytes a = rtp(0, 0, false, config), b = rtp(1, 0, true, idr);
        h.engine.processRtpVideoPacket(a.data(), a.size(), 1'500'000'000);
        h.engine.processRtpVideoPacket(b.data(), b.size(), 1'500'000'001);
        CHECK(h.frames.size() == 1);
        CHECK(!h.engine.needsKeyframe()); // the configured IDR cleared the flag
        h.engine.onTimer(1'600'000'000);
        CHECK(h.engine.stats().keyframeRequestsDispatched == 3); // the timer stops while clear
        const Bytes gap = rtp(3, 3000, true, pframe); // sequence gap: 1 -> 3
        h.engine.processRtpVideoPacket(gap.data(), gap.size(), 2'000'000'000);
        CHECK(h.frames.size() == 1); // the damaged AU is not delivered
        CHECK(h.engine.stats().recoverySignals >= 1);
        CHECK(h.lossCalls >= 1);
        CHECK(h.lastLossNs == 2'000'000'000);
        CHECK(h.engine.needsKeyframe());
        CHECK(h.engine.stats().keyframeRequestsDispatched == 4); // one dispatch, repeats throttled
        h.engine.onTimer(2'100'000'000); // retry resumes after the window
        CHECK(h.engine.stats().keyframeRequestsDispatched == 5);
        const Bytes c2 = rtp(4, 6000, false, config), i2 = rtp(5, 6000, true, idr);
        h.engine.processRtpVideoPacket(c2.data(), c2.size(), 2'200'000'000);
        h.engine.processRtpVideoPacket(i2.data(), i2.size(), 2'200'000'001);
        CHECK(h.frames.size() == 2);
        CHECK(!h.engine.needsKeyframe());
        h.engine.onTimer(3'000'000'000);
        CHECK(h.engine.stats().keyframeRequestsDispatched == 5);
        CHECK(h.keyframeChannels.size() == 5);
        for (KeyframeChannel c : h.keyframeChannels) CHECK(c == KeyframeChannel::Rtcp);
    }
    // 6: the DC path uses the sender-microsecond domain across the 32-bit wrap.
    {
        Harness h;
        h.engine.setGeneration(11);
        const Bytes k = dcPacket(0, 0xFFFFFFF0, true, 0, 1, key);
        h.engine.processDataChannelPacket(k.data(), k.size(), 10'000'000);
        CHECK(h.frames.size() == 1);
        CHECK(h.frames[0].timestampDomain == km::TimestampDomain::SenderMicroseconds);
        CHECK(h.frames[0].mediaTicks == int64_t{4294967280}); // 0xFFFFFFF0
        CHECK(h.frames[0].randomAccess);
        CHECK(h.frames[0].receivedMonotonicNs == 10'000'000);
        CHECK(h.frames[0].generation == 11);
        CHECK(h.engine.activePath() == MediaPath::DataChannel);
        CHECK(!h.engine.needsKeyframe());
        const Bytes d = dcPacket(1, 15, false, 0, 1, delta);
        h.engine.processDataChannelPacket(d.data(), d.size(), 11'000'000);
        CHECK(h.frames.size() == 2);
        CHECK(h.frames[1].mediaTicks == int64_t{4294967311}); // unwrapped past 2^32
        CHECK(!h.frames[1].randomAccess);
        CHECK(h.frames[1].receivedMonotonicNs == 11'000'000);
        CHECK(h.frames[1].generation == 11);
        CHECK(h.engine.stats().dataChannelPackets == 2);
        CHECK(h.engine.stats().dataChannelFrames == 2);
    }
    // 7: control passthrough - DC recover emits PLI/bitrate through the engine.
    {
        Harness h;
        h.engine.onTimer(100'000'000);
        CHECK(h.controls.size() == 2);
        CHECK(h.controls[0] == "{\"type\":\"pli\"}");
        CHECK(h.controls[1].find("{\"type\":\"bitrate\",\"bps\":") == 0);
        CHECK(h.engine.stats().controlMessagesOut == 2);
        h.engine.onTimer(110'000'000); // inside the 50ms PLI window
        CHECK(h.controls.size() == 2);
        h.engine.onTimer(151'000'000);
        CHECK(h.controls.size() == 4);
        CHECK(h.engine.stats().controlMessagesOut == 4);
        CHECK(h.engine.stats().keyframeRequestsDispatched == 2);
        CHECK(h.lossCalls == 0); // control recovery is not an RTP loss signal
    }
    // 8: reset clears packet/keyframe state, keeps configuration and cumulative stats.
    {
        Harness h;
        h.engine.setVideoPayloadTypes({96});
        h.engine.setGeneration(5);
        const Bytes k = dcPacket(0, 77, true, 0, 1, key);
        h.engine.processDataChannelPacket(k.data(), k.size(), 1'000'000);
        CHECK(h.frames.size() == 1);
        CHECK(h.engine.activePath() == MediaPath::DataChannel);
        const Bytes a = rtp(0, 0, false, config), b = rtp(1, 0, true, idr);
        h.engine.processRtpVideoPacket(a.data(), a.size(), 2'000'000);
        h.engine.processRtpVideoPacket(b.data(), b.size(), 3'000'000);
        CHECK(h.frames.size() == 1);
        CHECK(h.engine.stats().rtpPacketsDropped == 2); // gated by the active DC path
        h.engine.requestKeyframe(100'000'000);
        CHECK(h.engine.stats().keyframeRequestsDispatched == 1);
        CHECK(h.keyframeChannels[0] == KeyframeChannel::DataChannel);
        h.engine.requestKeyframe(101'000'000); // inside the window
        CHECK(h.engine.stats().keyframeRequestsDispatched == 1);
        const km::engine::ReceiverStats before = h.engine.stats();
        h.engine.reset();
        const km::engine::ReceiverStats afterReset = h.engine.stats();
        CHECK(afterReset.rtpPackets == before.rtpPackets); // stats are cumulative
        CHECK(afterReset.rtpPacketsDropped == before.rtpPacketsDropped);
        CHECK(afterReset.dataChannelFrames == before.dataChannelFrames);
        CHECK(afterReset.keyframeRequestsDispatched == before.keyframeRequestsDispatched);
        CHECK(h.engine.activePath() == MediaPath::Rtp);
        CHECK(h.engine.needsKeyframe());
        // The keyframe throttle was cleared: 0.5ms after the pre-reset request still dispatches.
        h.engine.requestKeyframe(100'500'000);
        CHECK(h.engine.stats().keyframeRequestsDispatched == 2);
        // Handlers, payload types and generation survive the reset.
        const Bytes c2 = rtp(9, 0x100, false, config), i2 = rtp(10, 0x100, true, idr);
        h.engine.processRtpVideoPacket(c2.data(), c2.size(), 102'000'000);
        h.engine.processRtpVideoPacket(i2.data(), i2.size(), 103'000'000);
        CHECK(h.frames.size() == 2);
        CHECK(h.frames[1].generation == 5);
        CHECK(h.frames[1].mediaTicks == 0x100);
        const Bytes wrong = rtp(11, 0x100, true, idr, 97);
        h.engine.processRtpVideoPacket(wrong.data(), wrong.size(), 104'000'000);
        CHECK(h.engine.stats().rtpPacketsDropped == 3); // payload types retained
        const Bytes dn = dcPacket(1, 5, false, 0, 1, delta);
        h.engine.processDataChannelPacket(dn.data(), dn.size(), 105'000'000);
        CHECK(h.frames.size() == 2); // the DC depacketizer was reset
        CHECK(h.engine.stats().dataChannelPackets == 2);
        CHECK(h.engine.stats().rtpPackets == 5);
        CHECK(h.engine.stats().rtpFrames == 1);
    }
    // Seeded malformed-packet corpus through the whole engine; not a full fuzz campaign.
    {
        Harness h;
        std::vector<uint8_t> all(128);
        for (int i = 0; i < 128; ++i) all[i] = uint8_t(i);
        h.engine.setVideoPayloadTypes(all); // let mutated payload types reach the depacketizers
        std::mt19937 rng(0x4b4d);
        uint64_t now = 1'000'000'000;
        for (int i = 0; i < 20000; ++i) {
            if (rng() & 1) {
                Bytes pkt = rtp(uint16_t(rng()), rng(), (rng() & 1) != 0, (rng() & 1) ? config : idr);
                for (int j = 0; j < 4; ++j) pkt[rng() % pkt.size()] = uint8_t(rng());
                h.engine.processRtpVideoPacket(pkt.data(), pkt.size(), now);
            } else {
                Bytes pkt = dcPacket(uint16_t(rng()), rng(), (rng() & 1) != 0, 0, 1, (rng() & 1) ? key : delta);
                for (int j = 0; j < 4; ++j) pkt[rng() % pkt.size()] = uint8_t(rng());
                if ((rng() & 7) == 0) pkt.resize(rng() % (pkt.size() + 1));
                h.engine.processDataChannelPacket(pkt.data(), pkt.size(), now);
            }
            if ((i % 16) == 0) h.engine.onTimer(now);
            now += 1'000'000;
            CHECK(h.engine.stats().rtpPackets >= h.engine.stats().rtpPacketsDropped);
        }
        CHECK(h.engine.stats().rtpPackets + h.engine.stats().dataChannelPackets == 20000);
    }
    CHECK(!std::filesystem::exists("debug_stream_dump.h264"));
    CHECK(!std::filesystem::exists("debug_stream_dump.jsonl"));
    std::cout << "receiver_engine: all checks passed, malformed corpus=20000\n";
}
