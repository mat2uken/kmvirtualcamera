#pragma once
#include <cstdint>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <limits>

namespace km {
// Decoded frames only: never use latest-wins for dependent H.264 access units.
// Immutable ownership allows each preview/camera consumer to retain its own frame.
template<class Frame> class LatestFrame {
public:
    uint64_t reset() {
        std::lock_guard lock(mutex_);
        if (generation_ == std::numeric_limits<uint64_t>::max())
            throw std::overflow_error("generation exhausted");
        frame_.reset(); arrivalNs_ = 0; return ++generation_;
    }
    uint64_t generation() const { std::lock_guard lock(mutex_); return generation_; }
    bool publish(uint64_t generation, std::shared_ptr<const Frame> frame, uint64_t arrivalNs) {
        std::lock_guard lock(mutex_);
        if (generation != generation_ || !frame || (frame_ && arrivalNs < arrivalNs_)) return false;
        frame_ = std::move(frame); arrivalNs_ = arrivalNs; return true;
    }
    std::shared_ptr<const Frame> get(uint64_t nowNs, uint64_t maxAgeNs) const {
        std::lock_guard lock(mutex_);
        if (!frame_ || nowNs < arrivalNs_ || nowNs-arrivalNs_ > maxAgeNs) return {};
        return frame_;
    }
private:
    mutable std::mutex mutex_;
    uint64_t generation_=1, arrivalNs_=0;
    std::shared_ptr<const Frame> frame_;
};
} // namespace km
