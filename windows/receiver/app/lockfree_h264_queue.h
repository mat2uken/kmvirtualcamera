#pragma once
#include "../../../shared/km/bounded_video_queue.h"
namespace km::app {
// Compatibility name only. The queue is now bounded and mutex-protected so it
// remains correct with both RTC media callbacks and control-thread resets.
class LockFreeH264Queue {
public:
    static constexpr size_t kCapacity = km::BoundedVideoQueue::kMaxFrames;
    bool Push(const uint8_t* p, size_t size, int64_t ts) { return p && queue_.push({p,size},ts,queue_.generation()); }
    bool Pop(std::vector<uint8_t>& bytes,int64_t& ts) {
        km::QueuedAccessUnit frame; if (!queue_.pop(frame)) return false;
        bytes=std::move(frame.bytes); ts=frame.timestampUs; return true;
    }
    bool IsEmpty() const { return queue_.queuedFrames() == 0; }
    size_t GetQueuedCount() const { return queue_.queuedFrames(); }
    void Clear() { queue_.reset(); }
private:
    km::BoundedVideoQueue queue_;
};
}
