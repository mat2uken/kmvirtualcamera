#include "opus_rtp_decoder.h"
#include <algorithm>
#include <optional>
#ifndef KM_ENABLE_OPUS
#define KM_ENABLE_OPUS 0
#endif
#if KM_ENABLE_OPUS
#if __has_include(<opus.h>)
#include <opus.h>
#elif __has_include(<opus/opus.h>)
#include <opus/opus.h>
#else
// Public, stable libopus decoder C ABI subset. This permits an installed system
// runtime to be tested without copying the upstream header bundle. Production
// FetchContent builds use the upstream header above. No codec implementation is vendored.
extern "C" {
struct OpusDecoder;
OpusDecoder* opus_decoder_create(int32_t, int, int*);
void opus_decoder_destroy(OpusDecoder*);
int opus_decode(OpusDecoder*, const unsigned char*, int32_t, int16_t*, int, int);
int opus_packet_get_nb_samples(const unsigned char*, int32_t, int32_t);
}
#endif
#endif
namespace km::audio {
struct OpusRtpDecoder::Impl {
    struct Pending { std::vector<uint8_t> bytes; uint32_t timestamp; int64_t arrival; };
    std::map<uint16_t, Pending> pending;
    Callback callback;
    uint8_t payloadType = 111;
    uint16_t expected = 0;
    std::optional<uint32_t> ssrc, endTimestamp;
    bool played = false;
#if KM_ENABLE_OPUS
    OpusDecoder* decoder = nullptr;
    ~Impl() { if (decoder) opus_decoder_destroy(decoder); }
    bool recreate() {
        if (decoder) opus_decoder_destroy(decoder);
        int error = 0; decoder = opus_decoder_create(48000, 2, &error); return decoder && error == 0;
    }
    bool decode(const uint8_t* bytes, size_t size, int capacity) {
        if (!decoder && !recreate()) return false;
        std::vector<int16_t> pcm(size_t(capacity) * 2);
        const int frames = opus_decode(decoder, bytes, int32_t(size), pcm.data(), capacity, 0);
        if (frames <= 0 || frames > capacity) return false;
        if (callback) callback(pcm.data(), size_t(frames) * 2, 2, 48000);
        return true;
    }
#else
    bool recreate() { return false; }
#endif
};
OpusRtpDecoder::OpusRtpDecoder() : impl_(std::make_unique<Impl>()) {}
OpusRtpDecoder::~OpusRtpDecoder() = default;
bool OpusRtpDecoder::Configure(uint8_t payloadType, Callback callback) {
    impl_->payloadType = payloadType; impl_->callback = std::move(callback);
    Reset(); return impl_->recreate();
}
void OpusRtpDecoder::Reset() {
    impl_->pending.clear(); impl_->ssrc.reset(); impl_->endTimestamp.reset(); impl_->played = false;
#if KM_ENABLE_OPUS
    if (impl_->decoder) { opus_decoder_destroy(impl_->decoder); impl_->decoder = nullptr; }
#endif
}
void OpusRtpDecoder::Receive(wire::Bytes bytes, int64_t now) {
#if KM_ENABLE_OPUS
    const auto packet = wire::parseRtp(bytes);
    if (!packet || packet->payloadType != impl_->payloadType || packet->payload.size() > 8192 || now < 0) return;
    if (impl_->ssrc && *impl_->ssrc != packet->ssrc) Reset();
    if (!impl_->ssrc) { impl_->ssrc = packet->ssrc; impl_->expected = packet->sequence; }
    const int delta = wire::sequenceDelta(packet->sequence, impl_->expected);
    if (delta < 0) {
        if (impl_->played || delta < -16) return;
        impl_->expected = packet->sequence; // initial jitter window may start out of order
    }
    if (impl_->pending.contains(packet->sequence)) return;
    if (impl_->pending.size() >= 16) {
        Reset(); impl_->ssrc = packet->ssrc; impl_->expected = packet->sequence;
    }
    impl_->pending.emplace(packet->sequence, Impl::Pending{
        std::vector<uint8_t>(packet->payload.begin(), packet->payload.end()), packet->timestamp, now});
    Tick(now);
#else
    (void)bytes; (void)now;
#endif
}
void OpusRtpDecoder::Tick(int64_t now) {
#if KM_ENABLE_OPUS
    for (int budget = 0; budget < 16 && !impl_->pending.empty(); ++budget) {
        auto it = impl_->pending.find(impl_->expected);
        if (it == impl_->pending.end()) {
            int nearest = 32768;
            for (auto candidate = impl_->pending.begin(); candidate != impl_->pending.end(); ++candidate) {
                const int distance = wire::sequenceDelta(candidate->first, impl_->expected);
                if (distance >= 0 && distance < nearest) { nearest = distance; it = candidate; }
            }
            if (it == impl_->pending.end() || now - it->second.arrival < 40000) break;
            impl_->expected = it->first;
        }
        if (now - it->second.arrival < 20000) break;
        auto packet = std::move(it->second); impl_->pending.erase(it); ++impl_->expected; impl_->played = true;
        if (now - packet.arrival > 120000) { impl_->recreate(); impl_->endTimestamp.reset(); continue; }
        const int duration = opus_packet_get_nb_samples(packet.bytes.data(), int32_t(packet.bytes.size()), 48000);
        if (duration <= 0 || duration > 5760) { impl_->recreate(); impl_->endTimestamp.reset(); continue; }
        if (impl_->endTimestamp) {
            const uint32_t raw = packet.timestamp - *impl_->endTimestamp;
            const int64_t gap = raw <= INT32_MAX ? int64_t(raw) : int64_t(raw) - (int64_t(1) << 32);
            if (gap < 0) continue; // stale timestamp; never regress decoder state
            if (gap > 0) {
                if (gap <= 5760 && gap % 120 == 0) impl_->decode(nullptr, 0, int(gap));
                else impl_->recreate();
            }
        }
        if (impl_->decode(packet.bytes.data(), packet.bytes.size(), 5760)) impl_->endTimestamp = packet.timestamp + uint32_t(duration);
        else { impl_->recreate(); impl_->endTimestamp.reset(); }
    }
#else
    (void)now;
#endif
}
} // namespace km::audio
