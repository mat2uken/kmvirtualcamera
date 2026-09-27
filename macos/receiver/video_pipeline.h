#pragma once
#include "km/receiver_contracts.h"
#include <CoreVideo/CoreVideo.h>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace km::mac {
// Receives normalized 720p 420v frames on the pipeline's worker thread. The
// buffer is valid only during the call; retain it if it must outlive the call.
// The handler must not block (the stage-6 publisher stores the newest image
// and returns immediately).
using NormalizedFrameHandler = std::function<void(CVPixelBufferRef)>;

struct PipelineStats {
    uint64_t accepted = 0, backpressure = 0, needKeyframe = 0, rejected = 0,
             staleGeneration = 0, decodeErrors = 0, normalizeErrors = 0, published = 0;
    // staleEpoch: decode results the worker dropped because their dependency
    //   chain had already been discarded (backpressure, start(), stop()).
    // Stage-8 timing, all on the pipeline's own steady clock.
    // queueHighWater: largest number of AUs waiting in the queue after a push.
    // decodeUs*: every AU the worker handed to the decoder, measured from
    //   submit() entry to the result being taken, including failures and
    //   configuration-only AUs. From stage 8 S2 on that span covers the wait
    //   behind the bounded window, so it is decoder latency, not VT time.
    // submitToPublishUs*: submit() entry to the frame handler call, counted
    //   once per delivered frame (so the count equals published).
    uint64_t staleEpoch = 0;
    uint64_t queueHighWater = 0;
    uint64_t decodeUsCount = 0, decodeUsTotal = 0, decodeUsMax = 0;
    uint64_t submitToPublishUsCount = 0, submitToPublishUsTotal = 0,
             submitToPublishUsMax = 0;
};

// Owner-executor IVideoPipeline: one worker thread owns the VideoToolbox
// decoder; submit() only enqueues the owning Annex-B AU on a bounded queue and
// never decodes. The worker fills the decoder's bounded window of outstanding
// AUs and takes results in submission order, so a frame that completes early
// can never overtake an earlier one. stop() joins the worker and invalidates
// all queued work before returning, so no handler runs after stop() returns.
//
// Recovery contract:
//  - a submit rejected as Backpressure discards the queued dependency chain
//    and bumps an epoch, so only a randomAccess frame accepted AFTER that
//    point clears the keyframe requirement;
//  - a decode failure re-arms the keyframe requirement; a normalize failure
//    drops only that frame because the decoder already advanced;
//  - a decode result whose epoch no longer matches (its chain was discarded)
//    is dropped before the handler and counted as staleEpoch;
//  - frames whose generation differs from start()'s, or malformed Annex-B,
//    are rejected without touching the queue.
class VideoPipeline final : public km::IVideoPipeline {
public:
    explicit VideoPipeline(NormalizedFrameHandler onFrame, size_t queueCapacity = 8);
    ~VideoPipeline() override;

    bool start(OutputFormat format, uint64_t generation, std::string& error) override;
    SubmitResult submit(EncodedVideoFrame frame) override;
    void setTransform(Transform transform) override;
    void stop() override;

    PipelineStats stats() const;

private:
    struct QueuedAu {
        EncodedVideoFrame frame;
        uint64_t epoch = 0;
        int64_t submittedNs = 0; // steady_clock at the submit() entry
    };
    void worker();

    const NormalizedFrameHandler onFrame_;
    const size_t capacity_;
    mutable std::mutex mutex_;
    std::condition_variable work_;
    std::thread worker_;
    std::deque<QueuedAu> queue_;
    bool running_ = false, stopping_ = false;
    uint64_t generation_ = 0, epoch_ = 0;
    // Set: the next non-randomAccess submit is rejected until a randomAccess
    // frame accepted under the current epoch decodes successfully.
    bool needKeyframe_ = true;
    std::atomic<int> rotation_{0};

    std::atomic<uint64_t> accepted_{0}, backpressure_{0}, needKeyframeCount_{0},
        rejected_{0}, staleGeneration_{0}, staleEpoch_{0}, decodeErrors_{0},
        normalizeErrors_{0}, published_{0};
    std::atomic<uint64_t> queueHighWater_{0};
    std::atomic<uint64_t> decodeUsCount_{0}, decodeUsTotal_{0}, decodeUsMax_{0};
    std::atomic<uint64_t> submitToPublishUsCount_{0}, submitToPublishUsTotal_{0},
        submitToPublishUsMax_{0};
};

std::unique_ptr<VideoPipeline> MakeVideoPipeline(NormalizedFrameHandler onFrame,
                                                 size_t queueCapacity = 8);
} // namespace km::mac
