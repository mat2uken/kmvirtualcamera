#import <CoreMediaIO/CMIOExtension.h>
#import <Foundation/Foundation.h>

// Stage 4 (W4-5): service startup only, verified against the Apple "Camera Extension"
// Xcode template (main.m creates the source, starts the service, runs the loop).
// Device/stream sources (stage 5 W5-1: provider.mm/device.mm/source_stream.mm) attach here later.
@interface KMExtensionProviderSource : NSObject <CMIOExtensionProviderSource>
@property(nonatomic, readonly) CMIOExtensionProvider* provider;
- (instancetype)initWithClientQueue:(dispatch_queue_t)clientQueue;
@end

@implementation KMExtensionProviderSource
@synthesize provider = _provider;

- (instancetype)initWithClientQueue:(dispatch_queue_t)clientQueue {
    if ((self = [super init])) {
        _provider = [[CMIOExtensionProvider alloc] initWithSource:self clientQueue:clientQueue];
    }
    return self;
}

- (BOOL)connectClient:(CMIOExtensionClient*)client error:(NSError* _Nullable*)outError {
    (void)client;
    (void)outError;
    // Stage 4 has no device/stream yet. Producer authentication is stage 6 (W6-2).
    return YES;
}

- (void)disconnectClient:(CMIOExtensionClient*)client {
    (void)client;
}

- (NSSet<CMIOExtensionProperty>*)availableProperties {
    return [NSSet setWithObject:CMIOExtensionPropertyProviderManufacturer];
}

- (nullable CMIOExtensionProviderProperties*)providerPropertiesForProperties:
    (NSSet<CMIOExtensionProperty>*)properties error:(NSError* _Nullable*)outError {
    (void)outError;
    CMIOExtensionProviderProperties* state =
        [CMIOExtensionProviderProperties providerPropertiesWithDictionary:@{}];
    if ([properties containsObject:CMIOExtensionPropertyProviderManufacturer])
        state.manufacturer = @"KMVirtualCamera";
    return state;
}

- (BOOL)setProviderProperties:(CMIOExtensionProviderProperties*)providerProperties
                        error:(NSError* _Nullable*)outError {
    (void)providerProperties;
    (void)outError;
    return YES;
}
@end

int main(int argc, const char* argv[]) {
    (void)argc;
    (void)argv;
    @autoreleasepool {
        KMExtensionProviderSource* source = [[KMExtensionProviderSource alloc] initWithClientQueue:nil];
        [CMIOExtensionProvider startServiceWithProvider:source.provider];
        CFRunLoopRun();
    }
    return 0;
}
