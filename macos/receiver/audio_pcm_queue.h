#pragma once
// Bounded hand-off between the RTC audio callback (single producer) and the
// device worker (single consumer). Stage 9 手順2 asks for a bounded PCM queue
// that never makes the RTC callback wait on an audio device:
//   - push() only takes the queue lock for the copy, reports how much it had to
//     drop and never waits for a device,
//   - pop() never waits for the producer; an empty pop is an underrun,
//   - the depth is capped, and a stalled device drops the oldest audio instead
//     of pinning the stream to the capacity latency.
// Accounting invariant (no external mutation): depth == pushed - dropped - popped.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace km::mac {

class AudioPcmQueue {
public:
    struct Stats {
        uint64_t pushedElements = 0;  // offered to push(), accepted or not
        uint64_t poppedElements = 0;  // handed to the consumer
        uint64_t droppedElements = 0; // lost: never accepted, or dropped from the front
        uint64_t underruns = 0;       // pop() found nothing to give
        uint64_t pushMaxNs = 0;       // longest push() including its lock
        size_t depthHigh = 0;
    };

    AudioPcmQueue(size_t capacityElements, size_t highWaterElements, size_t targetElements)
        : capacity_(capacityElements ? capacityElements : 1),
          highWater_(highWaterElements ? highWaterElements : capacity_),
          target_(std::min(targetElements, highWater_)),
          buffer_(capacity_) {}

    // Returns nothing: the loss is in Stats().droppedElements, because a front
    // drop can discard audio that an earlier push already accepted.
    void push(const int16_t* data, size_t elements) {
        const uint64_t t0 = NowNs();
        if (elements) {
            std::lock_guard<std::mutex> lock(m_);
            stats_.pushedElements += elements;
            size_t dropped = 0;
            // Capacity first: a push that does not fit drops the oldest audio,
            // and a push larger than the cap keeps its own newest tail.
            if (elements > capacity_ - depth_)
                dropped += DropFrontLocked(std::min(depth_, elements - (capacity_ - depth_)));
            if (elements > capacity_ - depth_) {
                const size_t skip = elements - (capacity_ - depth_);
                data += skip;
                elements -= skip;
                dropped += skip;
            }
            AppendLocked(data, elements);
            depth_ += elements;
            // Then the live policy: a depth over the high water mark drops back
            // to the target, so a stalled device cannot pin the stream at the
            // cap and a resumed consumer returns to the live depth.
            if (depth_ > highWater_ && depth_ > target_) dropped += DropFrontLocked(depth_ - target_);
            stats_.droppedElements += dropped;
            if (depth_ > stats_.depthHigh) stats_.depthHigh = depth_;
        }
        const uint64_t elapsed = NowNs() - t0;
        uint64_t seen = pushMaxNs_.load(std::memory_order_relaxed);
        while (elapsed > seen && !pushMaxNs_.compare_exchange_weak(seen, elapsed)) {}
    }

    // Returns the number of elements copied; 0 with a non-zero capacity is an
    // underrun and is counted.
    size_t pop(int16_t* out, size_t capacityElements) {
        std::lock_guard<std::mutex> lock(m_);
        const size_t take = std::min(capacityElements, depth_);
        if (take) {
            const size_t first = std::min(take, capacity_ - head_);
            std::copy_n(buffer_.data() + head_, first, out);
            if (take > first) std::copy_n(buffer_.data(), take - first, out + first);
            head_ = (head_ + take) % capacity_;
            depth_ -= take;
            stats_.poppedElements += take;
        } else if (capacityElements) {
            ++stats_.underruns;
        }
        return take;
    }

    // Discards what is queued (device restart, mute flush); the loss is counted
    // so the accounting invariant survives.
    void clear() {
        std::lock_guard<std::mutex> lock(m_);
        stats_.droppedElements += depth_;
        depth_ = 0;
        head_ = 0;
    }

    size_t depth() const {
        std::lock_guard<std::mutex> lock(m_);
        return depth_;
    }
    size_t capacity() const { return capacity_; }

    Stats stats() const {
        std::lock_guard<std::mutex> lock(m_);
        Stats out = stats_;
        out.pushMaxNs = pushMaxNs_.load(std::memory_order_relaxed);
        return out;
    }

private:
    static uint64_t NowNs() {
        return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }
    // Discards from the front; the caller reports the loss once, because the
    // same push can drop the front and part of its own input.
    size_t DropFrontLocked(size_t elements) {
        const size_t take = std::min(elements, depth_);
        head_ = (head_ + take) % capacity_;
        depth_ -= take;
        return take;
    }
    void AppendLocked(const int16_t* data, size_t elements) {
        size_t at = (head_ + depth_) % capacity_;
        const size_t first = std::min(elements, capacity_ - at);
        std::copy_n(data, first, buffer_.data() + at);
        if (elements > first) std::copy_n(data + first, elements - first, buffer_.data());
    }

    const size_t capacity_;
    const size_t highWater_;
    const size_t target_;

    mutable std::mutex m_;
    std::vector<int16_t> buffer_;
    size_t head_ = 0;
    size_t depth_ = 0;
    Stats stats_;
    std::atomic<uint64_t> pushMaxNs_{0};
};

} // namespace km::mac
