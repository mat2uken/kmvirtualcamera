#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <span>
#include <string>
#include <vector>
#include "../../common/data_channel_protocol.h"

namespace km::codec {
// Call packet/timer/reset on one serial owner. Callbacks run WITHOUT the mutex.
// Public legacy API is preserved for the Windows receiver.
class DcVideoDepacketizer {
public:
    using FrameCallback=std::function<void(const uint8_t*,size_t,int64_t)>;
    using ControlSendCallback=std::function<void(const std::string&)>;
    static constexpr size_t kMaxPendingFrames=32, kMaxPendingBytes=2*1024*1024;
    void SetCallback(FrameCallback cb);
    void SetControlSendCallback(ControlSendCallback cb);
    void Reset();
    void ProcessDataChannelPacket(const uint8_t* data,size_t size);
    void OnTimerTick();
    // Deterministic clock seam; callers supply nonnegative monotonic microseconds.
    void ProcessPacketAt(std::span<const uint8_t> packet,int64_t arrivalUs);
    void OnTimerTickAt(int64_t nowUs);
    uint32_t GetCurrentEstimatedBitrate() const { return bitrate_.load(); }
    uint64_t GetTotalFramesAssembled() const { return frames_.load(); }
    size_t GetPendingBytes() const;
    size_t GetPendingFrames() const;
private:
    struct Frame {
        uint32_t timestamp=0;
        uint8_t count=0,stableFlags=0;
        size_t received=0,bytes=0;
        int64_t arrival=0;
        std::vector<std::vector<uint8_t>> chunks;
    };
    struct Output { std::vector<uint8_t> data; uint32_t timestamp=0; };
    struct Batch {
        std::vector<Output> frames;
        std::vector<std::string> controls;
        FrameCallback frameCb; ControlSendCallback controlCb;
        uint64_t generation=0;
    };
    Batch batch() const;
    void dispatch(Batch& b);
    void drain(int64_t now,Batch& b);
    void recover(int64_t now,Batch& b);
    bool assemble(const Frame& frame,Output& output);
    void erase(std::map<uint16_t,Frame>::iterator it);
    mutable std::mutex mutex_;
    std::map<uint16_t,Frame> pending_;
    size_t bytes_=0;
    uint16_t expected_=0;
    bool initialized_=false,waiting_=true;
    int64_t lastPli_=-1,lastIncrease_=-1;
    std::vector<uint8_t> sps_,pps_;
    FrameCallback frameCb_; ControlSendCallback controlCb_;
    std::atomic<uint32_t> bitrate_{3000000};
    std::atomic<uint64_t> frames_{0},generation_{1};
};
} // namespace km::codec
