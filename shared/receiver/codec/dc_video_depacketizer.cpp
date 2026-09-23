#include "dc_video_depacketizer.h"
#include "../../km/h264.h"
#include <algorithm>
#include <chrono>
#include <utility>

namespace km::codec {
namespace {
int64_t NowUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
uint16_t LE16(const uint8_t* p) { return uint16_t(p[0]) | (uint16_t(p[1])<<8); }
uint32_t LE32(const uint8_t* p) { return uint32_t(LE16(p)) | (uint32_t(LE16(p+2))<<16); }
int Delta(uint16_t a,uint16_t b) { const unsigned d=uint16_t(a-b);return d<32768?int(d):int(d)-65536; }
}
void DcVideoDepacketizer::SetCallback(FrameCallback cb) { std::lock_guard l(mutex_);frameCb_=std::move(cb); }
void DcVideoDepacketizer::SetControlSendCallback(ControlSendCallback cb) { std::lock_guard l(mutex_);controlCb_=std::move(cb); }
void DcVideoDepacketizer::Reset() {
    std::lock_guard l(mutex_);
    pending_.clear();bytes_=0;expected_=0;initialized_=false;waiting_=true;
    sps_.clear();pps_.clear();lastPli_=lastIncrease_=-1;bitrate_=3000000;frames_=0;++generation_;
}
size_t DcVideoDepacketizer::GetPendingBytes() const { std::lock_guard l(mutex_);return bytes_; }
size_t DcVideoDepacketizer::GetPendingFrames() const { std::lock_guard l(mutex_);return pending_.size(); }
DcVideoDepacketizer::Batch DcVideoDepacketizer::batch() const {
    Batch b;b.frameCb=frameCb_;b.controlCb=controlCb_;b.generation=generation_.load();return b;
}
void DcVideoDepacketizer::dispatch(Batch& b) {
    for (auto& c:b.controls) {
        if (b.generation!=generation_.load()) return;
        if (b.controlCb) b.controlCb(c);
    }
    for (auto& f:b.frames) {
        if (b.generation!=generation_.load()) return;
        if (b.frameCb) b.frameCb(f.data.data(),f.data.size(),f.timestamp);
    }
}
void DcVideoDepacketizer::erase(std::map<uint16_t,Frame>::iterator it) { bytes_-=it->second.bytes;pending_.erase(it); }
void DcVideoDepacketizer::recover(int64_t now,Batch& b) {
    waiting_=true;
    if (lastPli_<0 || now-lastPli_>=50000) {
        lastPli_=lastIncrease_=now;
        bitrate_=std::max(2000000u,bitrate_.load()*85u/100u);
        b.controls.push_back("{\"type\":\"pli\"}");
        b.controls.push_back("{\"type\":\"bitrate\",\"bps\":"+std::to_string(bitrate_.load())+"}");
    }
}
void DcVideoDepacketizer::ProcessDataChannelPacket(const uint8_t* data,size_t size) {
    if (data) ProcessPacketAt({data,size},NowUs());
}
void DcVideoDepacketizer::OnTimerTick() { OnTimerTickAt(NowUs()); }
void DcVideoDepacketizer::OnTimerTickAt(int64_t now) {
    if (now<0) return;
    Batch b;
    { std::lock_guard l(mutex_);b=batch();drain(now,b);if (waiting_) recover(now,b); }
    dispatch(b);
}
void DcVideoDepacketizer::ProcessPacketAt(std::span<const uint8_t> p,int64_t now) {
    if (now<0 || p.size()<=12 || p.size()>dc_protocol::kMaxDcPacketSize ||
        LE16(p.data())!=dc_protocol::kDcMagic || p[3]!=dc_protocol::kPayloadH264 ||
        (p[2]&0xf0) || !p[5] || p[4]>=p[5]) return;
    const uint8_t flags=p[2],index=p[4],count=p[5];
    if (((flags&4)!=0)!=(index==0) || ((flags&8)!=0)!=(index+1==count)) return;
    const uint16_t seq=LE16(p.data()+6);const uint32_t timestamp=LE32(p.data()+8);
    Batch b;
    {
        std::lock_guard l(mutex_);b=batch();
        // Expire even while waiting for the first keyframe (the old implementation leaked here).
        drain(now,b);
        if (initialized_ && Delta(seq,expected_)<0) { /* stale */ }
        else {
            auto it=pending_.find(seq);
            const size_t payload=p.size()-12;
            if ((it==pending_.end() && pending_.size()>=kMaxPendingFrames) || bytes_+payload>kMaxPendingBytes) {
                pending_.clear();bytes_=0;initialized_=false;recover(now,b);
            } else {
                if (it==pending_.end()) {
                    Frame f;f.timestamp=timestamp;f.count=count;f.stableFlags=flags&3;
                    f.arrival=now;f.chunks.resize(count);it=pending_.emplace(seq,std::move(f)).first;
                }
                Frame& f=it->second;
                if (f.count!=count || f.timestamp!=timestamp || f.stableFlags!=(flags&3)) {
                    erase(it);recover(now,b);
                } else if (f.chunks[index].empty()) {
                    f.chunks[index].assign(p.begin()+12,p.end());++f.received;f.bytes+=payload;bytes_+=payload;
                } else if (!std::equal(f.chunks[index].begin(),f.chunks[index].end(),p.begin()+12,p.end())) {
                    erase(it);recover(now,b);
                }
                drain(now,b);
            }
        }
    }
    dispatch(b);
}
bool DcVideoDepacketizer::assemble(const Frame& frame,Output& out) {
    std::vector<uint8_t> raw;raw.reserve(frame.bytes);
    for (const auto& chunk:frame.chunks) raw.insert(raw.end(),chunk.begin(),chunk.end());
    std::vector<h264::Bytes> nalus;
    // Prefer the advertised Annex-B; accept legacy four-byte AVCC only when fully valid.
    if (!h264::SplitAnnexB(raw,nalus) && !h264::SplitLengthPrefixed4(raw,nalus)) return false;
    bool idr=false;std::vector<uint8_t> sps,pps;
    for (auto n:nalus) {
        switch (n[0]&31) {
        case 5: idr=true;break;
        case 7: sps.assign(n.begin(),n.end());break;
        case 8: pps.assign(n.begin(),n.end());break;
        default: break;
        }
    }
    // Never combine a new SPS with a stale PPS after a camera/resolution switch.
    if (!sps.empty() || !pps.empty()) {
        if (sps.empty() || pps.empty()) { sps_.clear();pps_.clear();return false; }
        sps_=std::move(sps);pps_=std::move(pps);
    }
    if ((frame.stableFlags&1) && !idr) return false; // header flags are not trusted
    if (waiting_ && !idr) return false;
    if (idr && (sps_.empty() || pps_.empty())) return false;
    out.data={0,0,0,1,9,0xf0};out.timestamp=frame.timestamp;
    if (idr) { h264::AppendAnnexB(out.data,sps_);h264::AppendAnnexB(out.data,pps_); }
    for (auto n:nalus) {
        const auto type=n[0]&31;
        if (type==9 || (idr && (type==7 || type==8))) continue;
        h264::AppendAnnexB(out.data,n);
    }
    if (idr) waiting_=false;
    return true;
}
void DcVideoDepacketizer::drain(int64_t now,Batch& b) {
    for (auto it=pending_.begin();it!=pending_.end();) {
        if (now>=it->second.arrival && now-it->second.arrival>=60000) {
            const bool needed=!initialized_ || Delta(it->first,expected_)>=0;
            auto old=it++;erase(old);if (needed) recover(now,b);
        } else ++it;
    }
    if (!initialized_) {
        auto first=pending_.end();
        for (auto it=pending_.begin();it!=pending_.end();++it)
            if (it->second.received==it->second.count && (it->second.stableFlags&1) &&
                (first==pending_.end() || it->second.arrival<first->second.arrival)) first=it;
        if (first==pending_.end()) return;
        expected_=first->first;initialized_=true;
    }
    while (!pending_.empty()) {
        auto it=pending_.find(expected_);
        if (it!=pending_.end()) {
            if (it->second.received==it->second.count) {
                Output out;
                if (assemble(it->second,out)) { b.frames.push_back(std::move(out));++frames_; }
                else recover(now,b);
                erase(it);++expected_;continue;
            }
            if (now-it->second.arrival>=10000) { erase(it);++expected_;recover(now,b);continue; }
            break;
        }
        auto next=pending_.end();int distance=32768;
        for (auto candidate=pending_.begin();candidate!=pending_.end();++candidate) {
            const int d=Delta(candidate->first,expected_);
            if (d>=0 && d<distance) { distance=d;next=candidate; }
        }
        if (next==pending_.end()) break;
        if (now-next->second.arrival<10000) break;
        expected_=next->first;recover(now,b);
    }
    for (auto it=pending_.begin();it!=pending_.end();) {
        if (Delta(it->first,expected_)<0) { auto old=it++;erase(old); } else ++it;
    }
    if (!waiting_ && !b.frames.empty()) {
        if (lastIncrease_<0) lastIncrease_=now;
        else if (now-lastIncrease_>=2000000) {
            bitrate_=std::min(8000000u,bitrate_.load()+300000u);lastIncrease_=now;
            b.controls.push_back("{\"type\":\"bitrate\",\"bps\":"+std::to_string(bitrate_.load())+"}");
        }
    }
}
} // namespace km::codec
