#pragma once
#import <Foundation/Foundation.h>
#import <CoreMediaIO/CMIOExtension.h>

NS_ASSUME_NONNULL_BEGIN
// Stage 5 (W5-1): owns exactly one device and exposes the provider source.
// All callbacks and the generator timer run on the queue passed at init so the
// stage 6 relay can be moved onto the same single provider queue later.
API_AVAILABLE(macos(12.3))
@interface KMExtensionProviderSource : NSObject <CMIOExtensionProviderSource>
@property(nonatomic, readonly) CMIOExtensionProvider* provider;
- (instancetype)initWithClientQueue:(dispatch_queue_t)queue;
// Creates the single device and registers it with the provider. Call once after
// startServiceWithProvider: has started the service.
- (void)installDevice;
@end
NS_ASSUME_NONNULL_END
