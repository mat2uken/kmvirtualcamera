#import "device.h"
#import "source_stream.h"
#import "ids.h"
#import <IOKit/audio/IOAudioTypes.h>

@implementation KMDeviceSource {
    CMIOExtensionDevice* _device;
    KMSourceStream* _stream;
}

- (instancetype)initWithQueue:(dispatch_queue_t)queue {
    if ((self = [super init])) {
        _stream = [[KMSourceStream alloc] initWithQueue:queue];
        if (!_stream) return nil;
        // Stable device UUID from ids.h; display name alone never re-identifies.
        NSUUID* deviceID = [[NSUUID alloc] initWithUUIDString:kKMDeviceIDString];
        if (!deviceID) deviceID = [NSUUID UUID];
        _device = [[CMIOExtensionDevice alloc] initWithLocalizedName:@"KM Virtual Camera"
                                                            deviceID:deviceID
                                                       legacyDeviceID:nil
                                                               source:self];
        if (!_device) return nil;
        NSError* error = nil;
        if (![_device addStream:_stream.stream error:&error]) {
            NSLog(@"KMVirtualCamera: addStream failed: %@", error);
            return nil;
        }
    }
    return self;
}

- (CMIOExtensionDevice*)device {
    return _device;
}

- (KMSourceStream*)stream {
    return _stream;
}

#pragma mark - CMIOExtensionDeviceSource

- (NSSet<CMIOExtensionProperty>*)availableProperties {
    return [NSSet setWithArray:@[
        CMIOExtensionPropertyDeviceModel,
        CMIOExtensionPropertyDeviceTransportType,
        CMIOExtensionPropertyDeviceCanBeDefaultInputDevice,
    ]];
}

- (nullable CMIOExtensionDeviceProperties*)devicePropertiesForProperties:
    (NSSet<CMIOExtensionProperty>*)properties error:(NSError* _Nullable*)outError {
    (void)outError;
    CMIOExtensionPropertyAttributes* readonly = CMIOExtensionPropertyAttributes
        .readOnlyPropertyAttribute;
    NSMutableDictionary<CMIOExtensionProperty, CMIOExtensionPropertyState*>* dict =
        [NSMutableDictionary dictionary];
    if ([properties containsObject:CMIOExtensionPropertyDeviceModel])
        dict[CMIOExtensionPropertyDeviceModel] = [CMIOExtensionPropertyState
            propertyStateWithValue:@"KMVirtualCamera" attributes:readonly];
    if ([properties containsObject:CMIOExtensionPropertyDeviceTransportType])
        dict[CMIOExtensionPropertyDeviceTransportType] = [CMIOExtensionPropertyState
            propertyStateWithValue:@(kIOAudioDeviceTransportTypeVirtual) attributes:readonly];
    if ([properties containsObject:CMIOExtensionPropertyDeviceCanBeDefaultInputDevice])
        dict[CMIOExtensionPropertyDeviceCanBeDefaultInputDevice] = [CMIOExtensionPropertyState
            propertyStateWithValue:@YES];
    return [CMIOExtensionDeviceProperties devicePropertiesWithDictionary:dict];
}

- (BOOL)setDeviceProperties:(CMIOExtensionDeviceProperties*)deviceProperties
                      error:(NSError* _Nullable*)outError {
    (void)deviceProperties;
    (void)outError;
    return YES;
}

@end
