#import "provider.h"
#import "device.h"

@implementation KMExtensionProviderSource {
    CMIOExtensionProvider* _provider;
    KMDeviceSource* _deviceSource;
    dispatch_queue_t _queue;
}
@synthesize provider = _provider;

- (instancetype)initWithClientQueue:(dispatch_queue_t)queue {
    if ((self = [super init])) {
        _queue = queue;
        _provider = [[CMIOExtensionProvider alloc] initWithSource:self clientQueue:queue];
    }
    return self;
}

- (void)installDevice {
    if (_deviceSource) return;
    _deviceSource = [[KMDeviceSource alloc] initWithQueue:_queue];
    if (!_deviceSource) {
        NSLog(@"KMVirtualCamera: device creation failed");
        return;
    }
    NSError* error = nil;
    if (![_provider addDevice:_deviceSource.device error:&error])
        NSLog(@"KMVirtualCamera: addDevice failed: %@", error);
}

#pragma mark - CMIOExtensionProviderSource

- (BOOL)connectClient:(CMIOExtensionClient*)client error:(NSError* _Nullable*)outError {
    (void)client;
    (void)outError;
    // Stage 5 accepts every client. Producer authentication (stage 6, W6-2) checks
    // the sink consumer separately at stream start.
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
