#import "sink_stream.h"
#import "source_stream.h"
#import "device.h"
#import "ids.h"

@implementation KMSinkStream {
    dispatch_queue_t _queue;
    CMIOExtensionStream* _stream;
    CMVideoFormatDescriptionRef _formatDescription;
}

- (instancetype)initWithQueue:(dispatch_queue_t)queue {
    if ((self = [super init])) {
        _queue = queue;
        _formatDescription = KMCreateFixedFormatDescription();
        if (!_formatDescription) return nil;
        _stream = [[CMIOExtensionStream alloc] initWithLocalizedName:@"Publisher"
                                                            streamID:[[NSUUID alloc]
                                                                          initWithUUIDString:
                                                                              kKMSinkStreamIDString]
                                                           direction:CMIOExtensionStreamDirectionSink
                                                           clockType:CMIOExtensionStreamClockTypeHostTime
                                                               source:self];
        if (!_stream) {
            NSLog(@"KMSinkStream: CMIOExtensionStream(sink) creation failed");
            return nil;
        }
    }
    return self;
}

- (void)dealloc {
    if (_formatDescription) CFRelease(_formatDescription);
}

- (CMIOExtensionStream*)stream {
    return _stream;
}

#pragma mark - CMIOExtensionStreamSource

- (NSArray<CMIOExtensionStreamFormat*>*)formats {
    const CMTime frameDuration = CMTimeMake(1, 30);
    return @[ [[CMIOExtensionStreamFormat alloc] initWithFormatDescription:_formatDescription
                                                          maxFrameDuration:frameDuration
                                                          minFrameDuration:frameDuration
                                                        validFrameDurations:nil] ];
}

- (NSSet<CMIOExtensionProperty>*)availableProperties {
    return [NSSet setWithArray:@[
        CMIOExtensionPropertyStreamActiveFormatIndex,
        CMIOExtensionPropertyStreamFrameDuration,
        CMIOExtensionPropertyStreamMaxFrameDuration,
    ]];
}

- (nullable CMIOExtensionStreamProperties*)streamPropertiesForProperties:
    (NSSet<CMIOExtensionProperty>*)properties error:(NSError* _Nullable*)outError {
    (void)outError;
    const CMTime frameDuration = CMTimeMake(1, 30);
    NSDictionary* durationDict = (__bridge_transfer NSDictionary*)
        CMTimeCopyAsDictionary(frameDuration, kCFAllocatorDefault);
    CMIOExtensionPropertyAttributes* readonly = CMIOExtensionPropertyAttributes
        .readOnlyPropertyAttribute;
    NSMutableDictionary<CMIOExtensionProperty, CMIOExtensionPropertyState*>* dict =
        [NSMutableDictionary dictionary];
    if ([properties containsObject:CMIOExtensionPropertyStreamActiveFormatIndex])
        dict[CMIOExtensionPropertyStreamActiveFormatIndex] = [CMIOExtensionPropertyState
            propertyStateWithValue:@0
                        attributes:[[CMIOExtensionPropertyAttributes alloc]
                                       initWithMinValue:@0
                                               maxValue:@0
                                          validValues:@[ @0 ]
                                             readOnly:NO]];
    if ([properties containsObject:CMIOExtensionPropertyStreamFrameDuration])
        dict[CMIOExtensionPropertyStreamFrameDuration] = [CMIOExtensionPropertyState
            propertyStateWithValue:durationDict attributes:readonly];
    if ([properties containsObject:CMIOExtensionPropertyStreamMaxFrameDuration])
        dict[CMIOExtensionPropertyStreamMaxFrameDuration] = [CMIOExtensionPropertyState
            propertyStateWithValue:durationDict attributes:readonly];
    return [CMIOExtensionStreamProperties streamPropertiesWithDictionary:dict];
}

- (BOOL)setStreamProperties:(CMIOExtensionStreamProperties*)streamProperties
                      error:(NSError* _Nullable*)outError {
    NSNumber* index = streamProperties.activeFormatIndex;
    if (index && index.intValue != 0) {
        if (outError)
            *outError = [NSError errorWithDomain:NSPOSIXErrorDomain code:EINVAL
                                        userInfo:@{NSLocalizedDescriptionKey :
                                                       @"Only format index 0 is available"}];
        return NO;
    }
    return YES;
}

- (BOOL)authorizedToStartStreamForClient:(CMIOExtensionClient*)client {
    // W6-2: only an OS-verified host may start the sink and become the producer.
    // The device rejects a second, different producer as well.
    KMDeviceSource* device = self.device;
    if (!device) {
        NSLog(@"KMSinkStream: sink start rejected: device not wired yet");
        return NO;
    }
    return [device sinkAuthorizeClient:client];
}

- (BOOL)startStreamAndReturnError:(NSError* _Nullable*)outError {
    (void)outError;
    // The producer was authorized in authorizedToStartStreamForClient: already;
    // frames arrive through the client queue and the relay consumes them.
    NSLog(@"KMSinkStream: startStream");
    return YES;
}

- (BOOL)stopStreamAndReturnError:(NSError* _Nullable*)outError {
    (void)outError;
    NSLog(@"KMSinkStream: stopStream");
    [self.device sinkStreamStopped];
    return YES;
}

@end
