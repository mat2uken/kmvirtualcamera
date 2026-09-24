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
    (void)outError;
    NSString* signingID = nil;
    if (@available(macOS 13.0, *)) signingID = client.signingID;
    // Record every identity the SDK actually exposes (W6-2 evidence). None of
    // these alone authorizes the producer - producer_auth.mm performs the
    // OS-checked verification when the sink stream is started.
    NSLog(@"KMProvider: connect client pid=%d signingID=%@ clientID=%@",
          client.pid, signingID ?: @"(n/a)", client.clientID);
    return YES;
}

- (void)disconnectClient:(CMIOExtensionClient*)client {
    // W6-2: a producer disconnect clears the relay producer (and its stale frames).
    [_deviceSource clientDisconnected:client];
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
