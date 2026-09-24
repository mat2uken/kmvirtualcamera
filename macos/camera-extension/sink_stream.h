#pragma once
#import <Foundation/Foundation.h>
#import <CoreMediaIO/CMIOExtension.h>

@class KMDeviceSource;
NS_ASSUME_NONNULL_BEGIN
// Stage 6 (W6-1): sink stream with its own stable ID (ids.h) and
// CMIOExtensionStreamDirectionSink, advertising the same fixed 720p30/420v/host-time
// format as the source. Producer authentication (W6-2) happens in
// authorizedToStartStreamForClient: via the device; stopStream clears the producer
// so consume stops and stale frames expire.
API_AVAILABLE(macos(12.3))
@interface KMSinkStream : NSObject <CMIOExtensionStreamSource>
@property(nonatomic, readonly) CMIOExtensionStream* stream;
// Set by the device right after construction; performs authorization decisions.
@property(nonatomic, weak, nullable) KMDeviceSource* device;
- (instancetype)initWithQueue:(dispatch_queue_t)queue;
@end
NS_ASSUME_NONNULL_END
