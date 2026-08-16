#pragma once

#include <cstdint>
#include <vector>
#include <atomic>
#include <span>
#include <cstring>

namespace km::app {

struct PreallocatedH264Slot {
    std::vector<uint8_t> buffer;
    size_t size{0};
    int64_t timestampUs{0};

    PreallocatedH264Slot() {
        buffer.resize(256 * 1024); // 256 KB pre-allocated per slot
    }
};

// Lock-Free Single-Producer Single-Consumer (SPSC) queue with pre-allocated slot buffers
class LockFreeH264Queue {
public:
    static constexpr size_t kCapacity = 32;

    LockFreeH264Queue() : head_(0), tail_(0) {
        slots_.resize(kCapacity);
    }

    bool Push(const uint8_t* data, size_t size, int64_t tsUs) {
        if (!data || size == 0) return false;

        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t tail = tail_.load(std::memory_order_acquire);

        if (((head + 1) % kCapacity) == tail) {
            // Buffer full
            return false;
        }

        auto& slot = slots_[head];
        if (slot.buffer.size() < size) {
            slot.buffer.resize(size);
        }
        std::memcpy(slot.buffer.data(), data, size);
        slot.size = size;
        slot.timestampUs = tsUs;

        head_.store((head + 1) % kCapacity, std::memory_order_release);
        return true;
    }

    bool Pop(std::vector<uint8_t>& outData, int64_t& outTsUs) {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        const size_t head = head_.load(std::memory_order_acquire);

        if (head == tail) {
            return false; // Queue empty
        }

        auto& slot = slots_[tail];
        if (outData.size() != slot.size) {
            outData.resize(slot.size);
        }
        std::memcpy(outData.data(), slot.buffer.data(), slot.size);
        outTsUs = slot.timestampUs;

        tail_.store((tail + 1) % kCapacity, std::memory_order_release);
        return true;
    }

    bool IsEmpty() const {
        return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_relaxed);
    }

    void Clear() {
        tail_.store(head_.load(std::memory_order_relaxed), std::memory_order_release);
    }

private:
    std::vector<PreallocatedH264Slot> slots_;
    alignas(64) std::atomic<size_t> head_;
    alignas(64) std::atomic<size_t> tail_;
};

} // namespace km::app
