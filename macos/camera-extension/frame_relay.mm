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
    _haveNotified = NO; _consuming = NO;
    _discontinuity = static_cast<CMIOExtensionStreamDiscontinuityFlags>(0);
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
            if (error) { [owner setAuthorizedProducer:nil]; return; }
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
    if (live && (!_haveNotified || _notified != _sequence)) {
        [_sink notifyScheduledOutputChanged:[[CMIOExtensionScheduledOutput alloc]
            initWithSequenceNumber:_sequence hostTimeInNanoseconds:nowNs]];
        _notified = _sequence; _haveNotified = YES;
    }
}
- (void)stop {
    ++_generation; _producer = nil; _latest = {}; _consuming = NO; _active = NO;
}
@end
