#pragma once
#import <Foundation/Foundation.h>
#include <CoreVideo/CoreVideo.h>

NS_ASSUME_NONNULL_BEGIN
// W6-3 publisher states, kept distinct for the UI exactly as the plan requires.
typedef NS_ENUM(NSInteger, KMSinkPublisherState) {
    KMSinkPublisherStateStopped = 0,      // stopped by request; no retry
    KMSinkPublisherStateUnavailable,      // device/sink stream not enumerated; retrying
    KMSinkPublisherStateOpening,          // enumerating / opening queue / starting stream
    KMSinkPublisherStateReady,            // stream started, frames enqueue
    KMSinkPublisherStateBackpressure,     // sink queue full; new frame dropped (auto-recovers)
    KMSinkPublisherStateDisconnected,     // was ready, connection lost; re-enumerating
};
typedef void (^KMSinkPublisherStateHandler)(KMSinkPublisherState state, NSString* detail);

// Stage 6 (W6-3/W6-4): enumerates the extension device by its stable UID
// (kCMIODevicePropertyDeviceUID), picks the direction==0 (output/sink) stream,
// owns the CMSimpleQueue from CMIOStreamCopyBufferQueue, starts/stops the stream,
// handles queue-altered callbacks and re-enumerates after extension restarts.
// Frames reach it via publishPixelBuffer: (non-blocking, newest image wins in the
// pending slot); one serial queue does normalize + sample create + enqueue.
API_AVAILABLE(macos(12.3))
@interface KMCMSinkPublisher : NSObject
@property(nonatomic, readonly) KMSinkPublisherState state;
// Invoked on the main queue whenever the state changes.
@property(nonatomic, copy, nullable) KMSinkPublisherStateHandler stateHandler;
// Requests running state: enumerates now and retries every 2s until found or -stop.
- (void)start;
// Stops the stream, unregisters the queue callback, drains and releases samples.
- (void)stop;
// Stores the newest image for the next feed tick; never blocks on queue/normalize.
- (void)publishPixelBuffer:(CVPixelBufferRef)buffer;
// W6-4 test hook: enqueues at ~4ms cadence for the given duration so the finite
// sink queue fills up, exercising backpressure + newest-frame replacement.
- (void)runFeedBurstForNanoseconds:(uint64_t)durationNs;
@end
NS_ASSUME_NONNULL_END
