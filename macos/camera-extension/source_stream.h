#pragma once
#import <Foundation/Foundation.h>
#import <CoreMediaIO/CMIOExtension.h>
#include <CoreMedia/CoreMedia.h>

NS_ASSUME_NONNULL_BEGIN
// Stage 5 (W5-2): fixed 1280x720 / 30fps / 420v source stream on the host clock.
// Stage 6 (W6-6): the stream no longer generates video itself. It counts consumer
// start/stop calls and reports changes through consumersChanged so the device can
// call KMFrameRelay.setSourceActive: and drive the single 30fps relay timer.
API_AVAILABLE(macos(12.3))
@interface KMSourceStream : NSObject <CMIOExtensionStreamSource>
@property(nonatomic, readonly) CMIOExtensionStream* stream;
// Number of unmatched start calls; 0 means no consumer captures the source.
@property(nonatomic, readonly) NSInteger activeConsumerCount;
// Invoked on the stream queue whenever the consumer count changes.
@property(nonatomic, copy, nullable) void (^consumersChanged)(NSInteger count);
- (instancetype)initWithQueue:(dispatch_queue_t)queue;
@end

// Creates the single fixed format description both streams advertise (each caller
// owns the returned +1 reference). The BT.709 extensions must exactly match the
// pixel-buffer attachments of every frame: a mismatch makes sample creation fail
// with kCMSampleBufferError_InvalidMediaFormat (-12743), the stage 5 bug.
FOUNDATION_EXTERN CMVideoFormatDescriptionRef _Nullable KMCreateFixedFormatDescription(void);
NS_ASSUME_NONNULL_END
