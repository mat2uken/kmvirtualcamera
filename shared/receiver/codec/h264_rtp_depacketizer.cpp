#include "h264_rtp_depacketizer.h"
#include <algorithm>
#include <utility>

namespace km::codec {
using km::wire::Bytes;
namespace {
bool validNal(Bytes n) { return !n.empty() && !(n[0] & 128) && (n[0] & 31) >= 1 && (n[0] & 31) <= 23; }
void append(std::vector<uint8_t>& dst, Bytes n) { dst.insert(dst.end(), {0,0,0,1}); dst.insert(dst.end(), n.begin(), n.end()); }
}
H264RtpDepacketizer::H264RtpDepacketizer(FrameCallback cb, KeyframeRequestCallback key)
    : callback_(std::move(cb)), keyframe_(std::move(key)) {}
void H264RtpDepacketizer::clearAu() {
    au_.clear(); fu_.clear(); damaged_ = idr_ = vcl_ = hasSps_ = hasPps_ = false;
}
void H264RtpDepacketizer::Reset() {
    clearAu(); sps_.clear(); pps_.clear();
    waiting_ = true; haveSsrc_ = haveSequence_ = haveTimestamp_ = false;
}
void H264RtpDepacketizer::lose() {
    au_.clear(); fu_.clear(); damaged_ = true; waiting_ = true;
    if (keyframe_) keyframe_();
}
bool H264RtpDepacketizer::appendNalu(Bytes n) {
    if (!validNal(n) || n.size() > kMaxAccessUnitBytes - 4 || au_.size() > kMaxAccessUnitBytes - n.size() - 4) return false;
    const uint8_t type = n[0] & 31;
    if ((type == 7 || type == 8) && n.size() > 65536) return false;
    if (type == 7) {
        if (!std::equal(sps_.begin(), sps_.end(), n.begin(), n.end())) pps_.clear();
        sps_.assign(n.begin(), n.end()); hasSps_ = true;
    } else if (type == 8) { pps_.assign(n.begin(), n.end()); hasPps_ = true; }
    idr_ |= type == 5; vcl_ |= type >= 1 && type <= 5;
    append(au_, n); return true;
}
void H264RtpDepacketizer::emit() {
    const bool good = !damaged_ && fu_.empty() && vcl_ && (!waiting_ || idr_) &&
        (!idr_ || (!sps_.empty() && !pps_.empty()));
    std::vector<uint8_t> output;
    if (good) {
        if (idr_) {
            if (!hasSps_) append(output, sps_);
            if (!hasPps_) append(output, pps_);
            waiting_ = false;
        }
        if (au_.size() > kMaxAccessUnitBytes - output.size()) { output.clear(); waiting_ = true; }
        else output.insert(output.end(), au_.begin(), au_.end());
    } else if (vcl_ || !fu_.empty()) waiting_ = true;
    const auto timestamp = timestamp_;
    clearAu();
    if (!output.empty()) { if (callback_) callback_(output.data(), output.size(), timestamp); }
    else if (waiting_ && keyframe_) keyframe_();
}
void H264RtpDepacketizer::ProcessRtpPacket(const uint8_t* bytes, size_t size) {
    if (!bytes) return;
    const auto packet = km::wire::parseRtp({bytes, size});
    if (!packet) { lose(); return; }
    if (haveSsrc_ && packet->ssrc != ssrc_) Reset();
    haveSsrc_ = true; ssrc_ = packet->ssrc;
    bool gap = false;
    if (haveSequence_) {
        const int delta = km::wire::sequenceDelta(packet->sequence, sequence_);
        if (delta <= 0) return; // never process stale payload after ignoring its sequence
        gap = delta != 1;
    }
    sequence_ = packet->sequence; haveSequence_ = true;
    if (haveTimestamp_ && packet->timestamp != timestamp_) {
        if (gap || !fu_.empty()) lose();
        emit(); // contiguous marker-less AU boundary is accepted
    }
    timestamp_ = packet->timestamp; haveTimestamp_ = true;
    if (gap) lose();
    if (damaged_) {
        if (packet->marker) { clearAu(); haveTimestamp_ = false; }
        return;
    }
    const auto p = packet->payload;
    if (p[0] & 128) { lose(); return; }
    const uint8_t type = p[0] & 31;
    bool ok = true;
    if (type >= 1 && type <= 23) {
        ok = fu_.empty() && appendNalu(p);
    } else if (type == 24) {
        std::vector<Bytes> nalus;
        size_t offset = 1, total = 0;
        while (offset < p.size()) {
            if (p.size() - offset < 2) { ok = false; break; }
            const size_t length = km::wire::be16(p.data() + offset); offset += 2;
            if (!length || length > p.size() - offset || nalus.size() >= 1024 ||
                !validNal(p.subspan(offset, length))) { ok = false; break; }
            total += length + 4; nalus.push_back(p.subspan(offset, length)); offset += length;
        }
        ok = ok && fu_.empty() && !nalus.empty() && total <= kMaxAccessUnitBytes - au_.size();
        if (ok) for (auto n : nalus) if (!appendNalu(n)) { ok = false; break; }
    } else if (type == 28) {
        if (p.size() < 3 || (p[1] & 32) || (p[1] & 31) == 0 || (p[1] & 31) > 23 || (p[1] & 192) == 192) ok = false;
        else {
            const bool first = p[1] & 128, last = p[1] & 64;
            const uint8_t header = (p[0] & 224) | (p[1] & 31);
            if (first) {
                if (!fu_.empty()) ok = false;
                else { fuHeader_ = header; fu_.push_back(header); }
            } else if (fu_.empty() || fuHeader_ != header) ok = false;
            if (ok) {
                const size_t length = p.size() - 2;
                if (length > kMaxAccessUnitBytes - 4 || fu_.size() > kMaxAccessUnitBytes - 4 - length ||
                    au_.size() > kMaxAccessUnitBytes - 4 - length - fu_.size()) ok = false;
                else {
                    fu_.insert(fu_.end(), p.begin() + 2, p.end());
                    if (last) { ok = appendNalu(fu_); fu_.clear(); }
                }
            }
        }
    } else ok = false; // interleaved mode (STAP-B/MTAP/FU-B) is not negotiated
    if (!ok) lose();
    if (packet->marker) { emit(); haveTimestamp_ = false; }
}
} // namespace km::codec
