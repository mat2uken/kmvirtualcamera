#import "frame_relay.h"
#include "../receiver/native_media.h"
#include <CoreMedia/CoreMedia.h>
#include <optional>

@implementation KMFrameRelay {
    CMIOExtensionStream* _source;
    CMIOExtensionStream* _sink;
    CMIOExtensionClient* _producer;
    dispatch_queue_t _queue;
    km::mac::PixelBuffer _latest, _black;
    uint64_t _generation, _arrival, _sequence, _notified;
    uint64_t _consumeCount, _sendCount;
    NSInteger _errorStreak;
    BOOL _consuming, _active, _haveNotified;
    std::optional<uint64_t> _lastOutput;
    CMIOExtensionStreamDiscontinuityFlags _discontinuity;
}
- (instancetype)initWithSource:(CMIOExtensionStream*)source sink:(CMIOExtensionStream*)sink
                         queue:(dispatch_queue_t)queue {
    if ((self = [super init])) {
        NSParameterAssert(source.direction == CMIOExtensionStreamDirectionSource);
        NSParameterAssert(sink.direction == CMIOExtensionStreamDirectionSink);
        _source = source; _sink = sink; _queue = queue; _generation = 1;
        std::string error;
        _black = km::mac::MakeBlack720p(error);
        if (!_black) return nil;
    }
    return self;
}
- (void)setAuthorizedProducer:(CMIOExtensionClient*)client {
    ++_generation; _producer = client; _latest = {}; _arrival = 0;
    _haveNotified = NO; _consuming = NO; _errorStreak = 0;
    _discontinuity = static_cast<CMIOExtensionStreamDiscontinuityFlags>(0);
    NSLog(@"KMFrameRelay: producer %s (gen=%llu)", client ? "set" : "cleared", _generation);
}
- (BOOL)hasAuthorizedProducer {
    return _producer != nil;
}
- (void)setSourceActive:(BOOL)active { _active = active; }
- (void)consumeOne {
    if (!_producer || _consuming) return;
    _consuming = YES;
    const uint64_t generation = _generation;
    __weak KMFrameRelay* weakSelf = self;
    [_sink consumeSampleBufferFromClient:_producer completionHandler:
        ^(CMSampleBufferRef sample, uint64_t sequence, CMIOExtensionStreamDiscontinuityFlags discontinuity,
          BOOL hasMore, NSError* error) {
        KMFrameRelay* strongSelf = weakSelf;
        if (!strongSelf) return;
        // Retain the borrowed image before crossing the callback lifetime/queue boundary.
        auto image = km::mac::PixelBuffer::retain(sample ? CMSampleBufferGetImageBuffer(sample) : nullptr);
        const CMTime host = CMClockGetTime(CMClockGetHostTimeClock());
        const int64_t ns = CMTimeConvertScale(host, 1000000000, kCMTimeRoundingMethod_Default).value;
        dispatch_async(strongSelf->_queue, ^{
            KMFrameRelay* owner = weakSelf;
            if (!owner || owner->_generation != generation) return;
            owner->_consuming = NO;
            if (error) {
                // W6-5: an error ends this consume. The producer is dropped only
                // after a sustained streak - a short empty-queue burst while the
                // host starts feeding must not kill the connection.
                ++owner->_errorStreak;
                NSLog(@"KMFrameRelay: consume error seq=%llu gen=%llu streak=%ld domain=%@ code=%ld",
                      sequence, generation, static_cast<long>(owner->_errorStreak),
                      error.domain, static_cast<long>(error.code));
                if (owner->_errorStreak >= 10) {
                    NSLog(@"KMFrameRelay: dropping producer after %ld consecutive errors",
                          static_cast<long>(owner->_errorStreak));
                    [owner setAuthorizedProducer:nil];
                }
                return;
            }
            owner->_errorStreak = 0;
            ++owner->_consumeCount;
            if (owner->_consumeCount <= 3 || owner->_consumeCount % 30 == 0) {
                NSLog(@"KMFrameRelay: consume n=%llu seq=%llu gen=%llu more=%d disc=%u hostNs=%llu",
                      owner->_consumeCount, sequence, generation, hasMore,
                      static_cast<unsigned>(discontinuity),
                      ns >= 0 ? static_cast<unsigned long long>(ns) : 0ull);
            }
            if (image && ns >= 0 && CVPixelBufferGetWidth(image.get()) == 1280 &&
                CVPixelBufferGetHeight(image.get()) == 720 &&
                CVPixelBufferGetPixelFormatType(image.get()) == kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange) {
                owner->_latest = image; owner->_arrival = uint64_t(ns);
                owner->_sequence = sequence; owner->_discontinuity = discontinuity;
            }
            // One outstanding consume per generation; do not recursively call inline.
            if (hasMore) dispatch_async(owner->_queue, ^{ [weakSelf consumeOne]; });
        });
    }];
}
- (void)tickAtHostTimeNs:(uint64_t)nowNs {
    // Owner drives absolute RationalPacer deadlines. Sender timestamps are not host timestamps.
    [self consumeOne];
    if (nowNs > INT64_MAX) return;
    const BOOL live = _latest && nowNs >= _arrival && nowNs - _arrival <= 1000000000ull;
    CVPixelBufferRef image = live ? _latest.get() : _black.get();
    if (!live) _latest = {}; // Clear stale imagery even when nobody captures the source.
    if (!_active || (_lastOutput && nowNs <= *_lastOutput)) return;
    CMVideoFormatDescriptionRef format = nullptr;
    CMSampleBufferRef sample = nullptr;
    OSStatus status = CMVideoFormatDescriptionCreateForImageBuffer(kCFAllocatorDefault, image, &format);
    CMSampleTimingInfo timing = {CMTimeMake(1, 30), CMTimeMake(int64_t(nowNs), 1000000000), kCMTimeInvalid};
    if (status == noErr) status = CMSampleBufferCreateForImageBuffer(kCFAllocatorDefault, image, true,
        nullptr, nullptr, format, &timing, &sample);
    if (format) CFRelease(format);
    if (status != noErr) return;
    [_source sendSampleBuffer:sample discontinuity:_discontinuity hostTimeInNanoseconds:nowNs];
    CFRelease(sample);
    _lastOutput = nowNs;
    _discontinuity = static_cast<CMIOExtensionStreamDiscontinuityFlags>(0);
    ++_sendCount;
    if (live && (!_haveNotified || _notified != _sequence)) {
        [_sink notifyScheduledOutputChanged:[[CMIOExtensionScheduledOutput alloc]
            initWithSequenceNumber:_sequence hostTimeInNanoseconds:nowNs]];
        _notified = _sequence; _haveNotified = YES;
    }
    // Periodic send log: sequence + scheduled time for stage 6 correlation
    // (host enqueue -> consume seq -> source send time).
    if (_sendCount <= 3 || _sendCount % 150 == 0) {
        NSLog(@"KMFrameRelay: send n=%llu seq=%llu pts=%llu live=%d active=%d",
              _sendCount, _sequence, nowNs, live, _active);
    }
}
- (void)stop {
    ++_generation; _producer = nil; _latest = {}; _consuming = NO; _active = NO;
}
@end
