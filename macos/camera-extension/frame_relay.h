#pragma once
#import <Foundation/Foundation.h>
#import <CoreMediaIO/CoreMediaIO.h>

// Integration component, NOT a registered Camera Extension by itself.
// Its owner constructs matching 720p30 HOST-TIME source/sink streams, authenticates
// one producer, counts source clients and calls every method on the given serial queue.
// A timer must keep ticking while a producer exists, even with no source clients.
API_AVAILABLE(macos(12.3))
@interface KMFrameRelay : NSObject
- (instancetype)initWithSource:(CMIOExtensionStream*)source sink:(CMIOExtensionStream*)sink
                         queue:(dispatch_queue_t)queue;
- (void)setAuthorizedProducer:(CMIOExtensionClient*)client;
- (void)setSourceActive:(BOOL)active;
- (void)tickAtHostTimeNs:(uint64_t)nowNs;
- (void)stop;
@end
