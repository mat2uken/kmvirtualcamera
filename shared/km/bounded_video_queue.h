#pragma once
#include <cstdint>
#include <deque>
#include <mutex>
#include <span>
#include <vector>
#include "h264.h"
namespace km {
struct QueuedAccessUnit {
    std::vector<uint8_t> bytes;
    int64_t timestampUs = 0;
    uint64_t generation = 0, resetSerial = 0;
};
// MPSC-safe compressed queue. Overflow invalidates all dependencies and requires
// a configured IDR; it is NOT a latest-frame queue for arbitrary P/B frames.
class BoundedVideoQueue {
public:
    static constexpr size_t kMaxFrames = 8, kMaxBytes = 2 * 1024 * 1024;
    static bool configuredIdr(std::span<const uint8_t> bytes) {
        std::vector<h264::Bytes> nalus; if (!h264::SplitAnnexB(bytes, nalus)) return false;
        bool sps = false, pps = false, idr = false;
        for (auto n : nalus) { sps |= (n[0] & 31) == 7; pps |= (n[0] & 31) == 8; idr |= (n[0] & 31) == 5; }
        return sps && pps && idr;
    }
    uint64_t reset() {
        std::lock_guard lock(mutex_); queue_.clear(); bytes_ = 0; waiting_ = true; ++resetSerial_; return ++generation_;
    }
    void invalidate() {
        std::lock_guard lock(mutex_); queue_.clear(); bytes_ = 0; waiting_ = true; ++resetSerial_;
    }
    uint64_t generation() const { std::lock_guard lock(mutex_); return generation_; }
    bool push(std::span<const uint8_t> bytes, int64_t timestampUs, uint64_t generation) {
        if (bytes.empty()) return false;
        const bool key = configuredIdr(bytes);
        std::lock_guard lock(mutex_);
        if (generation != generation_) return false;
        if (bytes.size() > kMaxBytes || queue_.size() >= kMaxFrames || bytes.size() > kMaxBytes - bytes_) {
            queue_.clear(); bytes_ = 0; waiting_ = true; ++resetSerial_;
            // Report overload even when this AU is itself an IDR; obtain a fresh one.
            return false;
        }
        if (waiting_ && !key) return false;
        if (key) waiting_ = false;
        queue_.push_back({std::vector<uint8_t>(bytes.begin(), bytes.end()), timestampUs, generation, resetSerial_});
        bytes_ += bytes.size(); return true;
    }
    bool pop(QueuedAccessUnit& out) {
        std::lock_guard lock(mutex_);
        if (queue_.empty()) return false;
        out = std::move(queue_.front()); bytes_ -= out.bytes.size(); queue_.pop_front(); return true;
    }
    bool current(const QueuedAccessUnit& frame) const {
        std::lock_guard lock(mutex_); return frame.generation == generation_ && frame.resetSerial == resetSerial_;
    }
    size_t queuedFrames() const { std::lock_guard lock(mutex_); return queue_.size(); }
    size_t queuedBytes() const { std::lock_guard lock(mutex_); return bytes_; }
private:
    mutable std::mutex mutex_; std::deque<QueuedAccessUnit> queue_;
    size_t bytes_ = 0; uint64_t generation_ = 1, resetSerial_ = 1; bool waiting_ = true;
};
} // namespace km
