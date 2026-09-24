#pragma once
#import <Foundation/Foundation.h>
#import <CoreMediaIO/CMIOExtension.h>

@class KMSourceStream, KMSinkStream;
NS_ASSUME_NONNULL_BEGIN
// Stage 5/6: owns the single device, both streams (source + sink), the KMFrameRelay
// and the ONE absolute-schedule 30fps timer that ticks the relay. All protocol
// callbacks run on the queue passed at init and the timer targets the same queue,
// so relay state is only touched from that single serial context (stage 6 note).
API_AVAILABLE(macos(12.3))
@interface KMDeviceSource : NSObject <CMIOExtensionDeviceSource>
@property(nonatomic, readonly) CMIOExtensionDevice* device;
@property(nonatomic, readonly) KMSourceStream* sourceStream;
@property(nonatomic, readonly) KMSinkStream* sinkStream;
- (instancetype)initWithQueue:(dispatch_queue_t)queue;

// W6-2: called from the sink stream's authorizedToStartStreamForClient:. Verifies
// the client is our host (OS-checked signing via producer_auth) and, on success,
// hands it to the relay with setAuthorizedProducer:. A second, different producer
// is rejected while one is connected.
- (BOOL)sinkAuthorizeClient:(CMIOExtensionClient*)client;
// Sink stream stopped: drop the producer so consume stops and stale frames expire.
- (void)sinkStreamStopped;
// The provider forwards disconnects; clears the producer if it was that client's.
- (void)clientDisconnected:(CMIOExtensionClient*)client;
@end
NS_ASSUME_NONNULL_END
