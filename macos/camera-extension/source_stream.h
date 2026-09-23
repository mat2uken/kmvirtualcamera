#pragma once
#import <Foundation/Foundation.h>
#import <CoreMediaIO/CMIOExtension.h>

NS_ASSUME_NONNULL_BEGIN
// Stage 5 (W5-2/W5-3): fixed 1280x720 / 30fps / 420v source stream on the host
// clock. Generates a moving shape plus a frame counter and paces sends on
// absolute host time. Consumer start/stop calls are counted so one consumer
// stopping never stops the output; stage 6 connects that count to
// KMFrameRelay.setSourceActive:.
API_AVAILABLE(macos(12.3))
@interface KMSourceStream : NSObject <CMIOExtensionStreamSource>
@property(nonatomic, readonly) CMIOExtensionStream* stream;
// Number of unmatched start calls; 0 means the generator is stopped.
@property(nonatomic, readonly) NSInteger activeConsumerCount;
- (instancetype)initWithQueue:(dispatch_queue_t)queue;
@end
NS_ASSUME_NONNULL_END
