// AudioPcmQueue contract test: bounded depth, drop accounting, underruns and
// the RTC-vs-device thread split. Pure C++, no audio hardware, no fixtures.
#include "receiver/audio_pcm_queue.h"

#include <atomic>
#include <cstdio>
#include <thread>
#include <vector>

namespace {
int failures = 0;
#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);             \
            ++failures;                                                            \
        }                                                                          \
    } while (0)

// depth == pushed - dropped - popped holds after every operation.
void CheckInvariant(const km::mac::AudioPcmQueue& q) {
    const auto s = q.stats();
    CHECK(s.pushedElements >= s.droppedElements + s.poppedElements);
    CHECK(q.depth() == s.pushedElements - s.droppedElements - s.poppedElements);
}

void RoundTripAndWrap() {
    // high water == capacity: this case is about the ring, not the live policy.
    km::mac::AudioPcmQueue q(8, 8, 2);
    std::vector<int16_t> in{1, 2, 3, 4, 5, 6, 7, 8};
    q.push(in.data(), in.size());
    CHECK(q.depth() == 8);
    std::vector<int16_t> out(8, 0);
    CHECK(q.pop(out.data(), 8) == 8);
    CHECK(out == in);
    CHECK(q.stats().underruns == 0);
    CheckInvariant(q);

    // Push again so the write index wraps past the ring end.
    q.push(in.data(), 8);
    CHECK(q.pop(out.data(), 8) == 8);
    CHECK(out == in);
    CheckInvariant(q);

    // Wrap with a split copy: 6 in the ring, 2 after the restart.
    const std::vector<int16_t> again{9, 10, 11, 12, 13, 14, 15, 16, 17, 18};
    q.push(again.data(), again.size());
    CHECK(q.pop(out.data(), 8) == 8);
    CHECK(out[0] == 11 && out[7] == 18);
    CheckInvariant(q);
}

void OverflowDropsAndKeepsNewest() {
    km::mac::AudioPcmQueue q(10, 10, 4);
    std::vector<int16_t> in(10, 1);
    q.push(in.data(), 10);
    CHECK(q.depth() == 10);
    CHECK(q.stats().droppedElements == 0);
    // One push beyond the cap: the tail of the push survives, the head is lost.
    std::vector<int16_t> burst(6, 2);
    q.push(burst.data(), burst.size());
    CHECK(q.depth() == 10);
    CHECK(q.stats().droppedElements == 6);
    CheckInvariant(q);
    std::vector<int16_t> out(10, 0);
    CHECK(q.pop(out.data(), 10) == 10);
    CHECK(out[0] == 1 && out[3] == 1); // the oldest was dropped first
    CHECK(out[4] == 2 && out[9] == 2); // and the new audio stayed queued
}

void HighWaterDropsTheOldest() {
    km::mac::AudioPcmQueue q(100, 40, 10);
    std::vector<int16_t> in(30, 1);
    q.push(in.data(), 30);
    CHECK(q.depth() == 30); // still under the high water mark
    CHECK(q.stats().droppedElements == 0);
    // 30 + 20 lands over 40, so the front drops back to the target of 10.
    std::vector<int16_t> next(20, 2);
    q.push(next.data(), next.size());
    CHECK(q.depth() == 10);
    CHECK(q.stats().droppedElements == 40);
    CheckInvariant(q);
    std::vector<int16_t> out(10, 0);
    CHECK(q.pop(out.data(), 10) == 10);
    CHECK(out[0] == 2); // the stale audio went, the new audio stayed
}

void EmptyPopCountsAnUnderrun() {
    km::mac::AudioPcmQueue q(16, 8, 2);
    std::vector<int16_t> out(4, 0);
    CHECK(q.pop(out.data(), 4) == 0);
    CHECK(q.stats().underruns == 1);
    std::vector<int16_t> in(3, 7);
    q.push(in.data(), 3);
    CHECK(q.pop(out.data(), 4) == 3); // partial: not an underrun
    CHECK(q.stats().underruns == 1);
    CHECK(q.pop(out.data(), 4) == 0);
    CHECK(q.stats().underruns == 2);
    CheckInvariant(q);
}

void ClearKeepsTheAccounting() {
    km::mac::AudioPcmQueue q(16, 16, 2);
    std::vector<int16_t> in(9, 5);
    q.push(in.data(), 9);
    q.clear();
    CHECK(q.depth() == 0);
    CHECK(q.stats().droppedElements == 9);
    CheckInvariant(q);
    q.push(in.data(), 9);
    CHECK(q.depth() == 9);
    CheckInvariant(q);
}

void ProducerConsumerThreads() {
    km::mac::AudioPcmQueue q(4800, 2400, 960); // 50 ms cap, 10 ms target
    std::atomic<bool> done{false};
    std::atomic<uint64_t> produced{0};
    std::thread producer([&] {
        std::vector<int16_t> chunk(1920, 3); // 20 ms of 48 kHz stereo
        for (int i = 0; i < 2000; ++i) {
            q.push(chunk.data(), chunk.size());
            produced += chunk.size();
        }
        done = true;
    });
    uint64_t consumed = 0;
    std::vector<int16_t> out(1920);
    while (!done.load() || q.depth() > 0) {
        const size_t got = q.pop(out.data(), out.size());
        consumed += got;
        if (got == 0) std::this_thread::yield();
    }
    producer.join();
    const auto s = q.stats();
    CHECK(s.pushedElements == produced.load());
    CHECK(s.depthHigh <= q.capacity());
    CHECK(q.depth() == s.pushedElements - s.droppedElements - s.poppedElements);
    CHECK(consumed == s.poppedElements);
}
} // namespace

int main() {
    RoundTripAndWrap();
    OverflowDropsAndKeepsNewest();
    HighWaterDropsTheOldest();
    EmptyPopCountsAnUnderrun();
    ClearKeepsTheAccounting();
    ProducerConsumerThreads();
    if (failures) {
        std::printf("audio_pcm_queue: %d check(s) failed\n", failures);
        return 1;
    }
    std::printf("audio_pcm_queue: all checks passed\n");
    return 0;
}
