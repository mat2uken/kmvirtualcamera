#pragma once
#import <Foundation/Foundation.h>
#import <CoreMediaIO/CMIOExtension.h>

@class KMSourceStream;
NS_ASSUME_NONNULL_BEGIN
// Stage 5 (W5-1): one device with one source stream, registered under the stable
// UUID from ids.h. The stream is created here and attached with addStream:error:.
API_AVAILABLE(macos(12.3))
@interface KMDeviceSource : NSObject <CMIOExtensionDeviceSource>
@property(nonatomic, readonly) CMIOExtensionDevice* device;
@property(nonatomic, readonly) KMSourceStream* stream;
- (instancetype)initWithQueue:(dispatch_queue_t)queue;
@end
NS_ASSUME_NONNULL_END
