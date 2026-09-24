#include "video_pipeline.h"
#include "native_media.h"
#include "km/h264.h"
#import <Foundation/Foundation.h>

namespace km::mac {
VideoPipeline::VideoPipeline(NormalizedFrameHandler onFrame, size_t queueCapacity)
    : onFrame_(std::move(onFrame)), capacity_(queueCapacity ? queueCapacity : 1) {}

VideoPipeline::~VideoPipeline() { stop(); }

bool VideoPipeline::start(OutputFormat format, uint64_t generation, std::string& error) {
    if (format.width != 1280 || format.height != 720) {
        error = "IVideoPipeline supports 1280x720 output only, got " +
            std::to_string(format.width) + "x" + std::to_string(format.height);
        return false;
    }
    if (!format.fpsNumerator || !format.fpsDenominator) {
        error = "IVideoPipeline requires a positive frame rate";
        return false;
    }
    // Validation runs before teardown: a failed start leaves the current
    // session untouched. A successful start serializes on the previous worker.
    stop(); // start/stop/format change serialize on this call; old worker joins first.
    std::lock_guard lock(mutex_);
    running_ = true;
    stopping_ = false;
    generation_ = generation;
    ++epoch_; // frames queued or in flight under the previous epoch cannot clear keyframe state
    queue_.clear();
    needKeyframe_ = true; // a fresh pipeline must see SPS/PPS+IDR before any dependent frame
    accepted_ = backpressure_ = needKeyframeCount_ = rejected_ = staleGeneration_ =
        decodeErrors_ = normalizeErrors_ = published_ = 0;
    worker_ = std::thread(&VideoPipeline::worker, this);
    return true;
}

SubmitResult VideoPipeline::submit(EncodedVideoFrame frame) {
    std::lock_guard lock(mutex_);
    if (!running_ || stopping_) return SubmitResult::Stopped;
    if (frame.generation != generation_) {
        ++staleGeneration_;
        return SubmitResult::Stopped;
    }
    std::vector<h264::Bytes> nalus;
    if (!h264::SplitAnnexB(frame.annexB, nalus)) {
        ++rejected_;
        return SubmitResult::Error;
    }
    if (queue_.size() >= capacity_) {
        // Queue full: discard the old dependency chain and drop the new frame.
        ++epoch_;
        queue_.clear();
        needKeyframe_ = true;
        ++backpressure_;
        return SubmitResult::Backpressure;
    }
    if (needKeyframe_ && !frame.randomAccess) {
        ++needKeyframeCount_;
        return SubmitResult::NeedKeyframe;
    }
    if (frame.randomAccess) needKeyframe_ = false; // it precedes every later frame in FIFO order
    queue_.push_back(QueuedAu{std::move(frame), epoch_});
    ++accepted_;
    work_.notify_one();
    return SubmitResult::Accepted;
}

void VideoPipeline::setTransform(Transform transform) { rotation_ = transform.clockwiseRotation; }

void VideoPipeline::stop() {
    std::thread joining;
    {
        std::lock_guard lock(mutex_);
        if (!worker_.joinable()) {
            running_ = false;
            stopping_ = false;
            queue_.clear();
            return;
        }
        stopping_ = true;
        queue_.clear(); // invalidate queued work now; the worker exits before join returns
        work_.notify_all();
        joining = std::move(worker_);
    }
    joining.join();
    std::lock_guard lock(mutex_);
    running_ = false;
    stopping_ = false;
    queue_.clear();
}

PipelineStats VideoPipeline::stats() const {
    PipelineStats out;
    out.accepted = accepted_;
    out.backpressure = backpressure_;
    out.needKeyframe = needKeyframeCount_;
    out.rejected = rejected_;
    out.staleGeneration = staleGeneration_;
    out.decodeErrors = decodeErrors_;
    out.normalizeErrors = normalizeErrors_;
    out.published = published_;
    return out;
}

void VideoPipeline::worker() {
    // The decoder and every decoded surface live and die on this thread only.
    VideoToolboxDecoder decoder;
    for (;;) {
        QueuedAu item;
        {
            std::unique_lock lock(mutex_);
            work_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (stopping_) return; // stop() invalidated all queued work
            item = std::move(queue_.front());
            queue_.pop_front();
        }
        @autoreleasepool {
            const EncodedVideoFrame& frame = item.frame;
            int64_t ptsUs = 0;
            if (frame.timestampDomain == TimestampDomain::Rtp90kHz)
                ptsUs = frame.mediaTicks * 1000000 / 90000;
            else
                ptsUs = frame.mediaTicks;
            std::string error;
            PixelBuffer image = decoder.decode(frame.annexB, ptsUs, error);
            if (!image) {
                // State first, counter last: a reader that observes the counter
                // observes every preceding state update. An empty error means a
                // configuration-only AU, which is not a failure. An item from a
                // superseded epoch must not clobber the current requirement.
                if (!error.empty()) {
                    {
                        std::lock_guard lock(mutex_);
                        if (item.epoch == epoch_) needKeyframe_ = true;
                    }
                    ++decodeErrors_;
                }
                continue;
            }
            PixelBuffer normalized = Normalize720p(image.get(), rotation_.load(), error);
            if (!normalized) {
                // The decoder already advanced; the dependency chain survives a
                // normalization failure, so only the frame is lost.
                ++normalizeErrors_;
                continue;
            }
            if (onFrame_) onFrame_(normalized.get());
            ++published_;
        }
    }
}

std::unique_ptr<VideoPipeline> MakeVideoPipeline(NormalizedFrameHandler onFrame,
                                                 size_t queueCapacity) {
    return std::make_unique<VideoPipeline>(std::move(onFrame), queueCapacity);
}
} // namespace km::mac
