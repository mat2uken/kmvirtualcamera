#import "cmio_sink_publisher.h"
#import "../camera-extension/ids.h"
#import "../host/host_auth_signer.h"
#import "native_media.h"
#import <CoreMedia/CoreMedia.h>
#import <CoreMediaIO/CoreMediaIO.h>
#include <atomic>
#include <deque>
#include <string>
#include <vector>
#include <os/lock.h>

// W6-3/W6-4 implementation notes (see plan 06-sink-publisher.md):
// - Device identity: kCMIODevicePropertyDeviceUID (CFString, persistent) compared
//   case-insensitively against the stable extension device UUID. Display names are
//   never used to identify.
// - Stream identity: among the device's streams, kCMIOStreamPropertyDirection
//   UInt32 0 = output stream, 1 = input stream (CMIOHardwareStream.h). Verified on
//   device: opening direction==1 fired KMSourceStream: startStream in the extension
//   at the same instant, so the extension's SOURCE is DAL direction 1 (input) and
//   our SINK is direction 0 (output) - corroborated by
//   CMIOExtensionPropertyStreamSinkBufferQueueSize translating to
//   kCMIOStreamPropertyOutputBufferQueueSize. Enqueueing into the source's queue
//   caused the framework to remove/over-release our samples (the run-1/2 crashes),
//   so direction alone must select the sink.
// - Queue contract (CMSimpleQueue.h): the queue does NOT retain elements, and it
//   supports ONE enqueueing thread (us) and ONE dequeueing thread (the framework,
//   on behalf of the extension's consume). Therefore this publisher NEVER dequeues
//   during normal operation: a full queue drops the NEW frame (backpressure), while
//   the pending slot keeps only the newest image (newest-wins end to end).
// - Ownership (measured on-device, v16): a successful enqueue TRANSFERS our +1 to
//   the framework side - it releases the sample after consumeSampleBuffer, and
//   flushes whatever is still queued when the stream stops. Keeping a "reference"
//   in _held and CFRelease-ing it later double-frees (crash evidence:
//   work/records/w6-4-*-overrelease*.ips: SIGTRAP in CFRelease from both
//   syncConsumed and the old teardown grace block). So _held holds BORROWED
//   pointers for count accounting only and is never CFRelease-ed; the only
//   samples we release ourselves are ones that never entered the queue.
// Ownership table (W6-4, corrected after the over-release crashes):
//   create  -> _created (own +1 until enqueue decides)
//   enqueue ok      -> +1 transferred to the framework; _held tracks the pointer
//                      (borrowed) for consume accounting, never released here
//   enqueue full    -> CFRelease immediately + _dropped (never entered the queue)
//   enqueue other   -> CFRelease immediately + _failed, reconnect
//   observed gone   -> popped from _held + _released counted (framework freed it)
//   teardown        -> stop stream (framework flushes queued samples), unregister
//                      callback, drain queue pointers, count _held as released
//                      (crash evidence shows the framework already freed them)
//   invariant       -> created == released (counts framework releases too)

namespace {
constexpr int32_t kFps = 30;
constexpr int64_t kPeriodNs = 1000000000 / kFps;
constexpr int64_t kBurstPeriodNs = 1000000;             // backpressure test cadence
// Measured on-device: 4ms fed ~197fps while the extension's chained XPC drain
// ran ~200fps, so the queue hovered at depth 2-6 and never reached capacity
// (dropped stayed 0). 1ms pushes feed past the drain ceiling so the queue
// actually fills and the drop path runs.
constexpr uint64_t kConsumeWatchdogNs = 3000000000ull;  // no consume for 3s => broken
constexpr int64_t kRetryDelayNs = 2000000000ll;          // re-enumerate every 2s
constexpr int kWidth = 1280;
constexpr int kHeight = 720;

uint64_t HostNanos() {
    const CMTime host = CMClockGetTime(CMClockGetHostTimeClock());
    const CMTime ns = CMTimeConvertScale(host, 1000000000, kCMTimeRoundingMethod_Default);
    return ns.value > 0 ? static_cast<uint64_t>(ns.value) : 0;
}

NSString* KMStateName(KMSinkPublisherState state) {
    switch (state) {
        case KMSinkPublisherStateStopped: return @"stopped";
        case KMSinkPublisherStateUnavailable: return @"unavailable";
        case KMSinkPublisherStateOpening: return @"opening";
        case KMSinkPublisherStateReady: return @"ready";
        case KMSinkPublisherStateBackpressure: return @"backpressure";
        case KMSinkPublisherStateDisconnected: return @"disconnected";
    }
    return @"unknown";
}

// The host's fixed 720p/420v format description. Same BT.709 extensions as the
// extension-side helper (source_stream.mm): buffer attachments and format
// description extensions must match exactly or sample creation fails with -12743.
CMVideoFormatDescriptionRef KMCreateHostFormatDescription() {
    NSDictionary* extensions = @{
        (__bridge NSString*)kCVImageBufferYCbCrMatrixKey :
            (__bridge NSString*)kCVImageBufferYCbCrMatrix_ITU_R_709_2,
        (__bridge NSString*)kCVImageBufferColorPrimariesKey :
            (__bridge NSString*)kCVImageBufferColorPrimaries_ITU_R_709_2,
        (__bridge NSString*)kCVImageBufferTransferFunctionKey :
            (__bridge NSString*)kCVImageBufferTransferFunction_ITU_R_709_2,
    };
    CMVideoFormatDescriptionRef format = nullptr;
    const OSStatus status = CMVideoFormatDescriptionCreate(kCFAllocatorDefault,
        kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange, kWidth, kHeight,
        (__bridge CFDictionaryRef)extensions, &format);
    if (status != noErr) return nullptr;
    return format;
}

// Finds the CMIODevice whose kCMIODevicePropertyDeviceUID equals wanted
// (case-insensitive; the DAL reports the UUID uppercased). 0 when not present.
CMIODeviceID KMFindDeviceByUID(NSString* wanted) {
    const CMIOObjectPropertyAddress devicesAddress = {
        kCMIOHardwarePropertyDevices, kCMIOObjectPropertyScopeGlobal,
        kCMIOObjectPropertyElementWildcard,
    };
    UInt32 size = 0;
    if (CMIOObjectGetPropertyDataSize(kCMIOObjectSystemObject, &devicesAddress, 0, nullptr,
            &size) != noErr ||
        size == 0 || size > 8192)
        return 0;
    std::vector<CMIODeviceID> devices(size / sizeof(CMIODeviceID));
    UInt32 used = 0;
    if (CMIOObjectGetPropertyData(kCMIOObjectSystemObject, &devicesAddress, 0, nullptr, size,
            &used, devices.data()) != noErr)
        return 0;
    devices.resize(used / sizeof(CMIODeviceID));

    const CMIOObjectPropertyAddress uidAddress = {
        kCMIODevicePropertyDeviceUID, kCMIOObjectPropertyScopeGlobal,
        kCMIOObjectPropertyElementWildcard,
    };
    for (CMIODeviceID device : devices) {
        CFStringRef uid = nullptr;
        UInt32 uidSize = sizeof(uid);
        if (CMIOObjectGetPropertyData(device, &uidAddress, 0, nullptr, uidSize, &uidSize, &uid) ==
                noErr &&
            uid) {
            const BOOL match = [(__bridge NSString*)uid caseInsensitiveCompare:wanted] ==
                NSOrderedSame;
            CFRelease(uid);
            if (match) return device;
        }
    }
    return 0;
}
} // namespace

@interface KMCMSinkPublisher ()
- (void)noteQueueAlteredWithToken:(void*)token streamID:(CMIOStreamID)streamID;
@end

// Registered with CMIOStreamCopyBufferQueue; invoked by the stream when it alters
// the queue. For our sink (DAL direction 0 = output) that happens when the framework
// removes a buffer for the extension's consume - an extra observation channel next
// to the count-based sync. Runs on an unspecified framework thread, so only hop to
// the work queue from here.
static void KMSinkQueueAltered(CMIOStreamID streamID, void* token, void* refCon) {
    KMCMSinkPublisher* publisher = (__bridge KMCMSinkPublisher*)refCon;
    [publisher noteQueueAlteredWithToken:token streamID:streamID];
}

@implementation KMCMSinkPublisher {
    dispatch_queue_t _queue;
    std::atomic<NSInteger> _state;
    KMSinkPublisherStateHandler _stateHandler;
    BOOL _desired;
    dispatch_source_t _feedTimer;
    dispatch_source_t _retryTimer;
    CMIODeviceID _deviceID;
    CMIOStreamID _streamID;
    BOOL _streamStarted;
    CMSimpleQueueRef _bufferQueue;
    UInt32 _queueCapacity;
    CMVideoFormatDescriptionRef _format;
    // Borrowed pointers to samples we enqueued (ownership transferred to the
    // framework); used only to correlate queue count with consumption.
    std::deque<CMSampleBufferRef> _held;
    CVPixelBufferRef _pending;
    os_unfair_lock _pendingLock;
    uint64_t _feedStartNs;
    uint64_t _feedIndex;
    uint64_t _lastConsumeNs;
    uint64_t _burstUntilNs;
    uint64_t _created, _released, _enqueued, _dropped, _failed, _altered;
}

- (instancetype)init {
    if ((self = [super init])) {
        _queue = dispatch_queue_create("jp.km.sinkpublisher", DISPATCH_QUEUE_SERIAL);
        _state.store(KMSinkPublisherStateStopped);
        _pendingLock = OS_UNFAIR_LOCK_INIT;
    }
    return self;
}

- (KMSinkPublisherState)state {
    return static_cast<KMSinkPublisherState>(_state.load());
}

#pragma mark - public API

- (void)start {
    dispatch_async(_queue, ^{
        if (self->_desired) return;
        self->_desired = YES;
        [self open];
    });
}

- (void)stop {
    dispatch_async(_queue, ^{
        self->_desired = NO;
        self->_burstUntilNs = 0;
        [self cancelFeedTimer];
        [self cancelRetryTimer];
        [self teardown];
        [self setState:KMSinkPublisherStateStopped detail:@"stopped by request"];
    });
}

- (void)publishPixelBuffer:(CVPixelBufferRef)buffer {
    if (!buffer) return;
    CVPixelBufferRef retained = CVPixelBufferRetain(buffer);
    os_unfair_lock_lock(&_pendingLock);
    CVPixelBufferRef replaced = _pending;
    _pending = retained;  // newest wins; this path never waits on queue/normalize
    os_unfair_lock_unlock(&_pendingLock);
    if (replaced) CVPixelBufferRelease(replaced);
}

- (void)runFeedBurstForNanoseconds:(uint64_t)durationNs {
    dispatch_async(_queue, ^{
        if (!self->_feedTimer || !self->_bufferQueue) {
            NSLog(@"KMSinkPublisher: burst ignored (not ready)");
            return;
        }
        self->_burstUntilNs = HostNanos() + durationNs;
        NSLog(@"KMSinkPublisher: feed burst for %llu ns", durationNs);
        dispatch_source_set_timer(self->_feedTimer,
            dispatch_time(DISPATCH_TIME_NOW, 0), DISPATCH_TIME_FOREVER, 100000);
    });
}

#pragma mark - state

- (void)setState:(KMSinkPublisherState)state detail:(NSString*)detail {
    const KMSinkPublisherState old =
        static_cast<KMSinkPublisherState>(_state.load());
    if (old == state) return;  // detail-only changes stay out of the UI
    _state.store(state);
    NSLog(@"KMSinkPublisher: state %@ -> %@ (%@)", KMStateName(old), KMStateName(state), detail);
    KMSinkPublisherStateHandler handler = _stateHandler;
    if (handler) {
        dispatch_async(dispatch_get_main_queue(), ^{ handler(state, detail); });
    }
}

#pragma mark - open / enumeration (W6-3)

- (void)open {
    if (!_desired) return;
    [self cancelRetryTimer];
    [self cancelFeedTimer];
    [self teardown];  // clear leftovers from any previous attempt
    [self setState:KMSinkPublisherStateOpening detail:@"enumerating device"];
    if (![self locateSink]) {
        [self scheduleRetry];
        return;
    }
    OSStatus status = CMIOStreamCopyBufferQueue(_streamID, &KMSinkQueueAltered,
        (__bridge void*)self, &_bufferQueue);
    if (status != noErr || !_bufferQueue) {
        [self setState:KMSinkPublisherStateDisconnected
                detail:[NSString stringWithFormat:@"CopyBufferQueue failed status=%d",
                                                   static_cast<int>(status)]];
        [self teardown];
        [self scheduleRetry];
        return;
    }
    _queueCapacity = CMSimpleQueueGetCapacity(_bufferQueue);
    _format = KMCreateHostFormatDescription();
    if (!_format) {
        [self setState:KMSinkPublisherStateDisconnected
                detail:@"format description creation failed"];
        [self teardown];
        [self scheduleRetry];
        return;
    }
    // W6-2 mode K: prove producer identity BEFORE startStream - the extension
    // verifies the signature inside its startStream authorization callback, and
    // this call blocks until that returns, so the response must already be set.
    [self attestProducer];
    status = CMIODeviceStartStream(_deviceID, _streamID);
    if (status != noErr) {
        [self setState:KMSinkPublisherStateDisconnected
                detail:[NSString stringWithFormat:@"StartStream failed status=%d",
                                                   static_cast<int>(status)]];
        [self teardown];
        [self scheduleRetry];
        return;
    }
    _streamStarted = YES;
    const uint64_t now = HostNanos();
    _feedStartNs = now;
    _feedIndex = 1;
    _lastConsumeNs = now;
    [self startFeedTimer];
    [self setState:KMSinkPublisherStateReady
            detail:[NSString stringWithFormat:@"stream started, queue capacity=%u",
                                               _queueCapacity]];
}

// W6-2 mode K handshake, run on every open attempt right before startStream:
// read the extension's challenge property, sign (challenge || pid || now) with
// the producer auth key from this app's login keychain (ACL-checked by securityd
// against our live designated requirement at signing time), and write the
// response property back. The element encoding of the framework's 4cc_ bridge
// is probed: element 0 first (WWDC22 "main element is always zero"), then the
// raw four-char code '0000' - whichever spelling the extension can serve wins,
// and the same element is used for the response write. Failure is not fatal
// here: startStream then surfaces the extension's integer stage log instead.
- (void)attestProducer {
    NSData* challenge = nil;
    CMIOObjectPropertyElement element = kCMIOObjectPropertyElementMain;
    const CMIOObjectPropertyAddress challengeAddress = {
        kKMAuthChallengeSelector, kCMIOObjectPropertyScopeGlobal,
        kCMIOObjectPropertyElementMain,
    };
    const CMIOObjectPropertyAddress challengeAddressAlt = {
        kKMAuthChallengeSelector, kCMIOObjectPropertyScopeGlobal, '0000',
    };
    for (const CMIOObjectPropertyAddress* address = &challengeAddress;; address = &challengeAddressAlt) {
        UInt32 size = 0;
        OSStatus status = CMIOObjectGetPropertyDataSize(_deviceID, address, 0, nullptr, &size);
        if (status == noErr && size >= 8 && size <= 4096) {
            NSMutableData* buffer = [NSMutableData dataWithLength:size];
            UInt32 used = 0;
            status = CMIOObjectGetPropertyData(_deviceID, address, 0, nullptr, size, &used,
                                               buffer.mutableBytes);
            if (status == noErr && used > 0) {
                buffer.length = used;
                challenge = buffer;
                element = address->mElement;
                break;
            }
        }
        NSLog(@"KMSinkPublisher: challenge read failed element=%u status=%d",
              static_cast<unsigned>(address->mElement), static_cast<int>(status));
        if (address == &challengeAddressAlt) break;
    }
    if (!challenge) {
        NSLog(@"KMSinkPublisher: attestation skipped (challenge unavailable)");
        return;
    }
    int signStatus = 0;
    NSData* response = KMHostSignProducerAuth(challenge, &signStatus);
    if (!response) {
        NSLog(@"KMSinkPublisher: attestation unsigned status=%d", signStatus);
        return;
    }
    const CMIOObjectPropertyAddress responseAddress = {
        kKMAuthResponseSelector, kCMIOObjectPropertyScopeGlobal, element,
    };
    const OSStatus status = CMIOObjectSetPropertyData(_deviceID, &responseAddress, 0, nullptr,
                                                      (UInt32)response.length, response.bytes);
    NSLog(@"KMSinkPublisher: attestation written bytes=%lu status=%d",
          static_cast<unsigned long>(response.length), static_cast<int>(status));
}

// Enumerates the device by stable UID and picks the direction==0 (output = sink)
// stream.
// On failure sets unavailable (device/sink absent) and returns NO.
- (BOOL)locateSink {
    _deviceID = KMFindDeviceByUID(kKMDeviceIDString);
    if (!_deviceID) {
        [self setState:KMSinkPublisherStateUnavailable
                detail:@"device UID not found in CoreMediaIO"];
        return NO;
    }
    const CMIOObjectPropertyAddress streamsAddress = {
        kCMIODevicePropertyStreams, kCMIOObjectPropertyScopeGlobal,
        kCMIOObjectPropertyElementWildcard,
    };
    UInt32 size = 0;
    if (CMIOObjectGetPropertyDataSize(_deviceID, &streamsAddress, 0, nullptr, &size) != noErr ||
        size == 0 || size > 4096) {
        [self setState:KMSinkPublisherStateUnavailable detail:@"device has no streams"];
        return NO;
    }
    std::vector<CMIOStreamID> streams(size / sizeof(CMIOStreamID));
    UInt32 used = 0;
    if (CMIOObjectGetPropertyData(_deviceID, &streamsAddress, 0, nullptr, size, &used,
            streams.data()) != noErr) {
        [self setState:KMSinkPublisherStateUnavailable detail:@"stream list read failed"];
        return NO;
    }
    streams.resize(used / sizeof(CMIOStreamID));

    const CMIOObjectPropertyAddress directionAddress = {
        kCMIOStreamPropertyDirection, kCMIOObjectPropertyScopeGlobal,
        kCMIOObjectPropertyElementWildcard,
    };
    BOOL found = NO;
    for (CMIOStreamID stream : streams) {
        UInt32 direction = 0xFFFFFFFF;
        UInt32 directionSize = sizeof(direction);
        if (CMIOObjectGetPropertyData(stream, &directionAddress, 0, nullptr,
                directionSize, &directionSize, &direction) != noErr)
            continue;
        // Evidence for the direction contract: 1=input(source), 0=output(sink).
        NSLog(@"KMSinkPublisher: stream id=%u direction=%u (1=input/source, 0=output/sink)",
              static_cast<unsigned>(stream), static_cast<unsigned>(direction));
        if (direction == 0 && !found) {
            _streamID = stream;
            found = YES;
        }
    }
    if (!found) {
        [self setState:KMSinkPublisherStateUnavailable
                detail:@"sink-direction (output) stream not found"];
        return NO;
    }
    return YES;
}

#pragma mark - feed (W6-4)

- (void)startFeedTimer {
    _feedTimer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, _queue);
    if (!_feedTimer) return;
    __weak KMCMSinkPublisher* weakSelf = self;
    dispatch_source_set_event_handler(_feedTimer, ^{
        [weakSelf feedTick];
    });
    const uint64_t now = HostNanos();
    const uint64_t target = _feedStartNs + _feedIndex * kPeriodNs;
    dispatch_source_set_timer(_feedTimer, dispatch_time(DISPATCH_TIME_NOW,
                                                        int64_t(target > now ? target - now : 1)),
                              DISPATCH_TIME_FOREVER, 500000);
    dispatch_resume(_feedTimer);
}

- (void)cancelFeedTimer {
    if (!_feedTimer) return;
    dispatch_source_cancel(_feedTimer);
    _feedTimer = nil;
}

- (void)feedTick {
    if (!_feedTimer || !_bufferQueue) return;
    uint64_t now = HostNanos();
    const BOOL bursting = now < _burstUntilNs;
    uint64_t target;
    if (bursting) {
        // Backpressure test: ignore the 30fps schedule and feed as fast as the
        // burst lasts so the finite queue fills and drops start.
        target = now;
        [self syncConsumed];
    } else {
        target = _feedStartNs + _feedIndex * kPeriodNs;
        if (now > target + kPeriodNs) {
            // Fell behind: jump the schedule forward instead of bursting.
            _feedIndex += (now - target) / kPeriodNs;
            target = _feedStartNs + _feedIndex * kPeriodNs;
        }
        [self syncConsumed];
        // Watchdog: while the sink stream runs the extension consumes every relay
        // tick. Frames queued without any consumption for 3s mean the connection is
        // broken (e.g. extension restarted) even if enqueue still appears to work.
        const int32_t queued = CMSimpleQueueGetCount(_bufferQueue);
        if (queued > 0 && now > _lastConsumeNs + kConsumeWatchdogNs) {
            [self setState:KMSinkPublisherStateDisconnected
                    detail:@"extension stopped consuming (watchdog)"];
            [self reconnectSoon];
            return;
        }
    }
    os_unfair_lock_lock(&_pendingLock);
    CVPixelBufferRef pending = _pending ? CVPixelBufferRetain(_pending) : nullptr;
    os_unfair_lock_unlock(&_pendingLock);
    if (pending) {
        [self feedFrame:pending target:target];
        CVPixelBufferRelease(pending);
    }
    if (bursting) {
        dispatch_source_set_timer(_feedTimer,
            dispatch_time(DISPATCH_TIME_NOW, kBurstPeriodNs), DISPATCH_TIME_FOREVER, 100000);
        return;
    }
    ++_feedIndex;
    now = HostNanos();
    uint64_t nextTarget = _feedStartNs + _feedIndex * kPeriodNs;
    if (nextTarget <= now) {
        _feedIndex += (now - nextTarget) / kPeriodNs + 1;
        nextTarget = _feedStartNs + _feedIndex * kPeriodNs;
    }
    dispatch_source_set_timer(_feedTimer, dispatch_time(DISPATCH_TIME_NOW,
                                                        int64_t(nextTarget - now)),
                              DISPATCH_TIME_FOREVER, 500000);
}

- (void)feedFrame:(CVPixelBufferRef)frame target:(uint64_t)target {
    std::string error;
    km::mac::PixelBuffer normalized = km::mac::Normalize720p(frame, 0, error);
    if (!normalized) {
        ++_failed;
        if (_failed <= 3 || _failed % 150 == 0)
            NSLog(@"KMSinkPublisher: Normalize720p failed (%llu): %s", _failed, error.c_str());
        return;
    }
    // Buffer attachments must exactly match the format description extensions
    // (stage 5 lesson: mismatch = kCMSampleBufferError_InvalidMediaFormat -12743).
    CVBufferSetAttachment(normalized.get(), kCVImageBufferYCbCrMatrixKey,
        kCVImageBufferYCbCrMatrix_ITU_R_709_2, kCVAttachmentMode_ShouldPropagate);
    CVBufferSetAttachment(normalized.get(), kCVImageBufferColorPrimariesKey,
        kCVImageBufferColorPrimaries_ITU_R_709_2, kCVAttachmentMode_ShouldPropagate);
    CVBufferSetAttachment(normalized.get(), kCVImageBufferTransferFunctionKey,
        kCVImageBufferTransferFunction_ITU_R_709_2, kCVAttachmentMode_ShouldPropagate);

    CMSampleTimingInfo timing = {
        CMTimeMake(1, kFps),
        CMTimeMake(static_cast<int64_t>(target), 1000000000),
        kCMTimeInvalid,
    };
    CMSampleBufferRef sample = nullptr;
    OSStatus status = CMSampleBufferCreateForImageBuffer(kCFAllocatorDefault, normalized.get(),
        true, nullptr, nullptr, _format, &timing, &sample);
    if (status != noErr || !sample) {
        ++_failed;
        if (_failed <= 3 || _failed % 150 == 0)
            NSLog(@"KMSinkPublisher: sample create failed (%llu) status=%d", _failed,
                  static_cast<int>(status));
        return;
    }
    ++_created;

    status = CMSimpleQueueEnqueue(_bufferQueue, sample);
    if (status == kCMSimpleQueueError_QueueIsFull) {
        // Backpressure: the client may not dequeue (single-dequeuer contract), so
        // the NEW frame is dropped; the pending slot still holds the newest image,
        // which is enqueued as soon as the extension drains the queue.
        ++_dropped;
        CFRelease(sample);
        ++_released;
        if (_dropped <= 3 || _dropped % 30 == 0)
            NSLog(@"KMSinkPublisher: queue full, dropped new frame (%llu)", _dropped);
        [self setState:KMSinkPublisherStateBackpressure
                detail:[NSString stringWithFormat:@"queue full (dropped=%llu)", _dropped]];
        return;
    }
    if (status != noErr) {
        ++_failed;
        CFRelease(sample);
        ++_released;
        [self setState:KMSinkPublisherStateDisconnected
                detail:[NSString stringWithFormat:@"enqueue failed status=%d",
                                                   static_cast<int>(status)]];
        [self reconnectSoon];
        return;
    }
    _held.push_back(sample);  // borrowed: ownership already transferred on enqueue
    ++_enqueued;
    if (_enqueued <= 3 || _enqueued % 30 == 0) {
        NSLog(@"KMSinkPublisher: feed n=%llu target=%llu queued=%d held=%zu dropped=%llu",
              _enqueued, target, static_cast<int>(CMSimpleQueueGetCount(_bufferQueue)),
              _held.size(), _dropped);
    }
    [self setState:KMSinkPublisherStateReady detail:@"frame enqueued"];
}

// Observes how many samples the extension has consumed (queue count shrank relative
// to what we still track) and accounts for them - the framework already released
// them, so we only drop the borrowed pointers.
- (void)syncConsumed {
    if (!_bufferQueue) return;
    const int32_t inQueue = CMSimpleQueueGetCount(_bufferQueue);
    uint64_t consumed = 0;
    while (static_cast<int32_t>(_held.size()) > inQueue) {
        _held.pop_front();
        ++_released;
        ++consumed;
    }
    if (consumed > 0) {
        _lastConsumeNs = HostNanos();
        if (_lastConsumeNs - _feedStartNs < 1000000000ull || consumed > 1) {
            // Early samples + any multi-frame catch-up are worth seeing for the
            // consume-vs-feed correlation in the stage 6 records.
            NSLog(@"KMSinkPublisher: consume observed +%llu (held=%llu queued=%d)",
                  static_cast<unsigned long long>(consumed),
                  static_cast<unsigned long long>(_held.size()), static_cast<int>(inQueue));
        }
    }
}

#pragma mark - queue callback (W6-3)

- (void)noteQueueAlteredWithToken:(void*)token streamID:(CMIOStreamID)streamID {
    dispatch_async(_queue, ^{
        ++self->_altered;
        if (self->_altered <= 3 || self->_altered % 150 == 0) {
            NSLog(@"KMSinkPublisher: queue altered n=%llu stream=%u token=%p", self->_altered,
                  static_cast<unsigned>(streamID), token);
        }
    });
}

#pragma mark - teardown / retry / stop (W6-3)

- (void)reconnectSoon {
    [self cancelFeedTimer];
    [self teardown];
    [self scheduleRetry];
}

- (void)teardown {
    if (_streamStarted && _deviceID && _streamID) {
        const OSStatus status = CMIODeviceStopStream(_deviceID, _streamID);
        if (status != noErr)
            NSLog(@"KMSinkPublisher: StopStream failed status=%d", static_cast<int>(status));
        _streamStarted = NO;
    }
    if (_bufferQueue) {
        // Unregister the altered callback (NULL proc). A queue pointer is still
        // accepted, so release whatever extra reference comes back.
        CMSimpleQueueRef unregistered = nullptr;
        CMIOStreamCopyBufferQueue(_streamID, NULL, NULL, &unregistered);
        if (unregistered) {
            if (unregistered == _bufferQueue) {
                CFRelease(unregistered);  // second +1 on the same queue
            } else {
                while (CMSimpleQueueGetCount(unregistered) > 0) CMSimpleQueueDequeue(unregistered);
                CFRelease(unregistered);
            }
        }
        // The stream is stopped, so we are now the only dequeuer: drain leftovers.
        while (CMSimpleQueueGetCount(_bufferQueue) > 0) CMSimpleQueueDequeue(_bufferQueue);
        CFRelease(_bufferQueue);
        _bufferQueue = nullptr;
    }
    if (_format) {
        CFRelease(_format);
        _format = nullptr;
    }
    _deviceID = 0;
    _streamID = 0;
    // Samples we enqueued were freed by the framework (at consume, or when the
    // stream stopped) - see the ownership note at the top of this file. _held
    // holds borrowed pointers only, so just account for them; never CFRelease.
    const size_t leftovers = _held.size();
    _released += leftovers;
    _held.clear();
    [self logAccounting];
}

// Leak / double-release check: created == released whenever nothing is in flight.
- (void)logAccounting {
    NSLog(@"KMSinkPublisher: samples created=%llu enqueued=%llu dropped=%llu failed=%llu "
           @"released=%llu inFlight=%lld",
        _created, _enqueued, _dropped, _failed, _released,
        static_cast<long long>(_created) - static_cast<long long>(_released));
}

- (void)scheduleRetry {
    if (!_desired || _retryTimer) return;
    _retryTimer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, _queue);
    if (!_retryTimer) return;
    __weak KMCMSinkPublisher* weakSelf = self;
    dispatch_source_set_event_handler(_retryTimer, ^{
        KMCMSinkPublisher* strongSelf = weakSelf;
        if (!strongSelf) return;
        [strongSelf cancelRetryTimer];  // one-shot; open may schedule the next
        [strongSelf open];
    });
    dispatch_source_set_timer(_retryTimer,
        dispatch_time(DISPATCH_TIME_NOW, kRetryDelayNs), DISPATCH_TIME_FOREVER, 0);
    dispatch_resume(_retryTimer);
    NSLog(@"KMSinkPublisher: retry scheduled in 2s");
}

- (void)cancelRetryTimer {
    if (!_retryTimer) return;
    dispatch_source_cancel(_retryTimer);
    _retryTimer = nil;
}

- (void)cancelFeedTimerAndRetry {
    [self cancelFeedTimer];
    [self cancelRetryTimer];
}

@end
