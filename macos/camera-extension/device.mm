#import "device.h"
#import "source_stream.h"
#import "sink_stream.h"
#import "producer_auth.h"
#import "frame_relay.h"
#import "ids.h"
#import <IOKit/audio/IOAudioTypes.h>
#import <Security/Security.h>

// Mode K state helpers (declared here so init may call them in any order).
@interface KMDeviceSource ()
- (void)issueAuthChallenge;
@end

namespace {
constexpr int32_t kFps = 30;
constexpr int64_t kPeriodNs = 1000000000 / kFps;

// Host time in nanoseconds; same clock as CMIOExtensionStreamClockTypeHostTime.
uint64_t HostNanos() {
    const CMTime host = CMClockGetTime(CMClockGetHostTimeClock());
    const CMTime ns = CMTimeConvertScale(host, 1000000000, kCMTimeRoundingMethod_Default);
    return ns.value > 0 ? static_cast<uint64_t>(ns.value) : 0;
}
} // namespace

@implementation KMDeviceSource {
    CMIOExtensionDevice* _device;
    KMSourceStream* _sourceStream;
    KMSinkStream* _sinkStream;
    KMFrameRelay* _relay;
    dispatch_queue_t _queue;
    dispatch_source_t _timer;
    uint64_t _startNs;
    uint64_t _frameIndex;
    NSUUID* _producerClientID;
    // W6-2 mode K: the challenge currently served to clients, the challenge the
    // stored response must have been signed over (frozen at response time so a
    // repeated authorize call stays idempotent), and the stored response itself.
    NSData* _authChallenge;
    NSData* _authVerifyChallenge;
    NSData* _authResponse;
}

- (instancetype)initWithQueue:(dispatch_queue_t)queue {
    if ((self = [super init])) {
        _queue = queue;
        [self issueAuthChallenge];
        _sourceStream = [[KMSourceStream alloc] initWithQueue:queue];
        _sinkStream = [[KMSinkStream alloc] initWithQueue:queue];
        if (!_sourceStream || !_sinkStream) return nil;
        _relay = [[KMFrameRelay alloc] initWithSource:_sourceStream.stream
                                                  sink:_sinkStream.stream
                                                 queue:queue];
        if (!_relay) return nil;
        _sinkStream.device = self;

        // W6-6: consumer count -> setSourceActive: + relay timer policy.
        __weak KMDeviceSource* weakSelf = self;
        _sourceStream.consumersChanged = ^(NSInteger count) {
            KMDeviceSource* strongSelf = weakSelf;
            if (!strongSelf) return;
            [strongSelf->_relay setSourceActive:count > 0];
            [strongSelf updateRelayTimer];
        };

        // Stable device UUID from ids.h; display name alone never re-identifies.
        NSUUID* deviceID = [[NSUUID alloc] initWithUUIDString:kKMDeviceIDString];
        if (!deviceID) deviceID = [NSUUID UUID];
        _device = [[CMIOExtensionDevice alloc] initWithLocalizedName:@"KM Virtual Camera"
                                                            deviceID:deviceID
                                                       legacyDeviceID:nil
                                                               source:self];
        if (!_device) return nil;
        NSError* error = nil;
        if (![_device addStream:_sourceStream.stream error:&error]) {
            NSLog(@"KMDevice: addStream(source) failed: %@", error);
            return nil;
        }
        error = nil;
        if (![_device addStream:_sinkStream.stream error:&error]) {
            NSLog(@"KMDevice: addStream(sink) failed: %@", error);
            return nil;
        }
    }
    return self;
}

- (void)dealloc {
    [self stopRelayTimer];
}

- (CMIOExtensionDevice*)device {
    return _device;
}

- (KMSourceStream*)sourceStream {
    return _sourceStream;
}

- (KMSinkStream*)sinkStream {
    return _sinkStream;
}

#pragma mark - producer (W6-2)

// Fresh single-use challenge for mode K. Regenerated at init, whenever a
// response is accepted (setDeviceProperties), so a signed response is only ever
// valid for the challenge that was current when the host read it.
- (void)issueAuthChallenge {
    uint8_t bytes[kKMAuthChallengeLength];
    if (SecRandomCopyBytes(kSecRandomDefault, sizeof(bytes), bytes) != errSecSuccess) {
        NSLog(@"KMDevice: challenge regeneration failed status=1, keeping previous value");
        return;
    }
    _authChallenge = [NSData dataWithBytes:bytes length:sizeof(bytes)];
    // Without this push the CoreMediaIO side keeps serving the previously read
    // value: measured on-device, the 2nd+ attestations were signed over the
    // stale cached challenge and failed verification with stage=64 (only the
    // 1st attempt, where cache and live value coincided, succeeded).
    if (_device) {
        [_device notifyPropertiesChanged:@{
            kKMAuthChallengeProperty : [CMIOExtensionPropertyState
                propertyStateWithValue:_authChallenge
                            attributes:CMIOExtensionPropertyAttributes.readOnlyPropertyAttribute],
        }];
    }
}

- (BOOL)sinkAuthorizeClient:(CMIOExtensionClient*)client {
    if (_relay.hasAuthorizedProducer && ![client.clientID isEqual:_producerClientID]) {
        NSLog(@"KMDevice: sink start rejected: another producer connected (existing=%@ new=%@)",
              _producerClientID, client.clientID);
        return NO;
    }
    NSString* reason = nil;
    // Mode K first (in-band signature over the single-use challenge); the
    // file-based ladder remains as fallback evidence when it yields nothing.
    BOOL ok = KMVerifyKeyAttestation(client, _authVerifyChallenge, _authResponse, &reason);
    const char* mode = "key-attestation";
    if (!ok) {
        ok = KMVerifyHostProducer(client, &reason);
        mode = "os-check-ladder";
    }
    if (!ok) {
        // reason is redacted in the unified log; producer_auth also logs the
        // failing stage/status as integers.
        NSLog(@"KMDevice: sink start rejected (pid=%d): %@", client.pid, reason);
        return NO;
    }
    _producerClientID = client.clientID;
    [_relay setAuthorizedProducer:client];
    NSString* signingID = nil;
    if (@available(macOS 13.0, *)) signingID = client.signingID;
    NSLog(@"KMDevice: producer authorized (pid=%d signingID=%@ clientID=%@ mode=%s)",
          client.pid, signingID ?: @"(n/a)", client.clientID, mode);
    [self updateRelayTimer];
    return YES;
}

- (void)sinkStreamStopped {
    if (!_producerClientID) return;
    [self clearProducer:@"sink stopped"];
}

- (void)clientDisconnected:(CMIOExtensionClient*)client {
    if (!_producerClientID || ![client.clientID isEqual:_producerClientID]) return;
    [self clearProducer:@"client disconnected"];
}

- (void)clearProducer:(NSString*)why {
    NSLog(@"KMDevice: clearing producer (%@)", why);
    _producerClientID = nil;
    [_relay setAuthorizedProducer:nil];
    [self updateRelayTimer];
}

#pragma mark - relay timer (W6-6)

// Policy: tick while a producer exists (the sink must keep consuming and expiring
// old frames even with zero source consumers) or while at least one consumer
// captures the source (then the relay sends live frames or standby black).
- (void)updateRelayTimer {
    const BOOL want = _sourceStream.activeConsumerCount > 0 || _relay.hasAuthorizedProducer;
    if (want && !_timer) {
        [self startRelayTimer];
    } else if (!want && _timer) {
        [self stopRelayTimer];
    }
}

- (void)startRelayTimer {
    const uint64_t now = HostNanos();
    NSLog(@"KMDevice: relay timer starting (now=%llu)", now);
    if (_startNs == 0 || now < _startNs) _startNs = now;
    _frameIndex = (now - _startNs) / kPeriodNs + 1;
    _timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, _queue);
    if (!_timer) return;
    __weak KMDeviceSource* weakSelf = self;
    dispatch_source_set_event_handler(_timer, ^{
        [weakSelf relayTick];
    });
    const uint64_t target = _startNs + _frameIndex * kPeriodNs;
    dispatch_source_set_timer(_timer, dispatch_time(DISPATCH_TIME_NOW,
                                                    int64_t(target > now ? target - now : 1)),
                              DISPATCH_TIME_FOREVER, 500000);
    dispatch_resume(_timer);
}

- (void)stopRelayTimer {
    if (!_timer) return;
    NSLog(@"KMDevice: relay timer stopping");
    dispatch_source_cancel(_timer);
    _timer = nil;
}

- (void)relayTick {
    if (!_timer) return;
    uint64_t now = HostNanos();
    uint64_t target = _startNs + _frameIndex * kPeriodNs;
    if (now > target + kPeriodNs) {
        // Fell behind: jump the schedule forward instead of bursting (stage 5 rule).
        _frameIndex += (now - target) / kPeriodNs;
        target = _startNs + _frameIndex * kPeriodNs;
    }
    [_relay tickAtHostTimeNs:target];
    ++_frameIndex;
    now = HostNanos();
    uint64_t nextTarget = _startNs + _frameIndex * kPeriodNs;
    if (nextTarget <= now) {
        _frameIndex += (now - nextTarget) / kPeriodNs + 1;
        nextTarget = _startNs + _frameIndex * kPeriodNs;
    }
    dispatch_source_set_timer(_timer, dispatch_time(DISPATCH_TIME_NOW,
                                                    int64_t(nextTarget - now)),
                              DISPATCH_TIME_FOREVER, 500000);
}

#pragma mark - CMIOExtensionDeviceSource

- (NSSet<CMIOExtensionProperty>*)availableProperties {
    return [NSSet setWithArray:@[
        CMIOExtensionPropertyDeviceModel,
        CMIOExtensionPropertyDeviceTransportType,
        CMIOExtensionPropertyDeviceCanBeDefaultInputDevice,
        // W6-2 mode K channel (WWDC22 4cc_<selector>_<scope>_<element> bridge).
        kKMAuthChallengeProperty,
        kKMAuthResponseProperty,
    ]];
}

- (nullable CMIOExtensionDeviceProperties*)devicePropertiesForProperties:
    (NSSet<CMIOExtensionProperty>*)properties error:(NSError* _Nullable*)outError {
    (void)outError;
    NSMutableDictionary<CMIOExtensionProperty, CMIOExtensionPropertyState*>* dict =
        [NSMutableDictionary dictionary];
    CMIOExtensionPropertyAttributes* readonly = CMIOExtensionPropertyAttributes
        .readOnlyPropertyAttribute;
    if ([properties containsObject:CMIOExtensionPropertyDeviceModel])
        dict[CMIOExtensionPropertyDeviceModel] = [CMIOExtensionPropertyState
            propertyStateWithValue:@"KMVirtualCamera" attributes:readonly];
    if ([properties containsObject:CMIOExtensionPropertyDeviceTransportType])
        dict[CMIOExtensionPropertyDeviceTransportType] = [CMIOExtensionPropertyState
            propertyStateWithValue:@(kIOAudioDeviceTransportTypeVirtual) attributes:readonly];
    if ([properties containsObject:CMIOExtensionPropertyDeviceCanBeDefaultInputDevice])
        dict[CMIOExtensionPropertyDeviceCanBeDefaultInputDevice] =
            [CMIOExtensionPropertyState propertyStateWithValue:@YES];
    // Mode K: serve the current challenge (read-only for clients) and echo back
    // the stored response. The prefix match tolerates however the framework
    // re-spells the element part of the 4cc_ key on the way in.
    for (CMIOExtensionProperty key in properties) {
        if ([key hasPrefix:@"4cc_chlg_"]) {
            dict[key] = [CMIOExtensionPropertyState propertyStateWithValue:
                _authChallenge ?: [NSData data] attributes:readonly];
        } else if ([key hasPrefix:@"4cc_resp_"]) {
            dict[key] = [CMIOExtensionPropertyState propertyStateWithValue:
                _authResponse ?: [NSData data]];
        }
    }
    return [CMIOExtensionDeviceProperties devicePropertiesWithDictionary:dict];
}

- (BOOL)setDeviceProperties:(CMIOExtensionDeviceProperties*)deviceProperties
                      error:(NSError* _Nullable*)outError {
    (void)outError;
    // Mode K: the host writes its attestation response here BEFORE startStream.
    // Freeze the challenge this response must have been signed over, then issue
    // a fresh one so every response is single-use (a replayed or stolen response
    // is verified against a challenge it was never signed over). Freezing at
    // set-time keeps repeated authorize calls for the same response idempotent.
    for (CMIOExtensionProperty key in deviceProperties.propertiesDictionary) {
        if (![key hasPrefix:@"4cc_resp_"]) continue;
        id value = deviceProperties.propertiesDictionary[key].value;
        _authVerifyChallenge = _authChallenge;
        _authResponse = [value isKindOfClass:[NSData class]] ? value : [NSData data];
        [self issueAuthChallenge];
        NSLog(@"KMDevice: attestation response received bytes=%lu",
              static_cast<unsigned long>(_authResponse.length));
    }
    return YES;
}

@end
