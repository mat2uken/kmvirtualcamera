#import "source_stream.h"
#import "ids.h"
#import "../receiver/native_media.h"
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#include <cstring>
#include <string>

namespace {
constexpr int kWidth = 1280;
constexpr int kHeight = 720;
constexpr int32_t kFps = 30;
constexpr int64_t kPeriodNs = 1000000000 / kFps;

// Host time in nanoseconds; same clock as CMIOExtensionStreamClockTypeHostTime.
uint64_t HostNanos() {
    const CMTime host = CMClockGetTime(CMClockGetHostTimeClock());
    const CMTime ns = CMTimeConvertScale(host, 1000000000, kCMTimeRoundingMethod_Default);
    return ns.value > 0 ? static_cast<uint64_t>(ns.value) : 0;
}

struct Planes {
    uint8_t* y = nullptr;
    size_t yStride = 0;
    uint8_t* uv = nullptr;
    size_t uvStride = 0;
};

void FillBackground(Planes& p, uint8_t value) {
    for (int row = 0; row < kHeight; ++row) memset(p.y + row * p.yStride, value, kWidth);
    for (int row = 0; row < kHeight / 2; ++row) {
        uint8_t* line = p.uv + row * p.uvStride;
        for (int x = 0; x < kWidth; x += 2) {
            line[x] = 128;
            line[x + 1] = 128;
        }
    }
}

// Fills a rectangle; chroma is subsampled 2x2 so x/width must be even for color fills.
void FillRect(Planes& p, int x0, int y0, int w, int h, uint8_t yValue, int uvX, int uvW,
              uint8_t u, uint8_t v) {
    if (x0 < 0 || y0 < 0 || x0 + w > kWidth || y0 + h > kHeight) return;
    for (int row = y0; row < y0 + h; ++row) memset(p.y + row * p.yStride + x0, yValue, w);
    if (uvW <= 0) return;
    for (int row = y0 / 2; row < (y0 + h + 1) / 2; ++row) {
        uint8_t* line = p.uv + row * p.uvStride;
        for (int x = uvX; x < uvX + uvW; x += 2) {
            line[x] = u;
            line[x + 1] = v;
        }
    }
}

// 7-segment frame counter (stage 5 shows progress without any text framework).
// Segment bits: a=1 b=2 c=4 d=8 e=16 f=32 g=64.
constexpr uint8_t kDigits[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};

void DrawDigit(Planes& p, int digit, int x, int y, int thickness) {
    const int w = 4 * thickness, h = 8 * thickness, half = h / 2;
    const uint8_t seg = kDigits[digit % 10];
    auto bar = [&](int bx, int by, int bw, int bh) {
        FillRect(p, bx, by, bw, bh, 235, 0, 0, 0, 0);
    };
    if (seg & 1) bar(x, y, w, thickness);                             // a
    if (seg & 2) bar(x + w - thickness, y, thickness, half);          // b
    if (seg & 4) bar(x + w - thickness, y + half, thickness, half);   // c
    if (seg & 8) bar(x, y + h - thickness, w, thickness);             // d
    if (seg & 16) bar(x, y + half, thickness, half);                  // e
    if (seg & 32) bar(x, y, thickness, half);                         // f
    if (seg & 64) bar(x, y + half - thickness / 2, w, thickness);     // g
}

void DrawCounter(Planes& p, uint64_t value) {
    char digits[6];
    for (int i = 5; i >= 0; --i) {
        digits[i] = char(value % 10);
        value /= 10;
    }
    for (int i = 0; i < 6; ++i)
        DrawDigit(p, digits[i], 40 + i * 60, 40, 10);
}

km::mac::PixelBuffer MakeFrame(std::string& error) {
    CVPixelBufferRef raw = nullptr;
    NSDictionary* attrs = @{
        (__bridge NSString*)kCVPixelBufferIOSurfacePropertiesKey: @{},
        (__bridge NSString*)kCVPixelBufferMetalCompatibilityKey: @YES,
    };
    const CVReturn status = CVPixelBufferCreate(kCFAllocatorDefault, kWidth, kHeight,
        kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange, (__bridge CFDictionaryRef)attrs, &raw);
    if (status != kCVReturnSuccess) {
        error = "CVPixelBufferCreate: " + std::to_string(status);
        return {};
    }
    km::mac::PixelBuffer buffer(raw);
    if (CVPixelBufferLockBaseAddress(raw, 0) != kCVReturnSuccess) {
        error = "CVPixelBufferLockBaseAddress failed";
        return {};
    }
    {
        Planes planes;
        planes.y = static_cast<uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(raw, 0));
        planes.yStride = CVPixelBufferGetBytesPerRowOfPlane(raw, 0);
        planes.uv = static_cast<uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(raw, 1));
        planes.uvStride = CVPixelBufferGetBytesPerRowOfPlane(raw, 1);
        if (!planes.y || !planes.uv) {
            CVPixelBufferUnlockBaseAddress(raw, 0);
            error = "pixel buffer has no plane addresses";
            return {};
        }
        FillBackground(planes, 48);
        // Moving shape: triangle-wave x/y so any two frames differ. Exact color is
        // verified later (stage 8); stage 5 only needs a visible moving object.
        const int shapeW = 320, shapeH = 180;
        const int travelX = kWidth - shapeW, travelY = kHeight - shapeH;
        const uint64_t tick = HostNanos() / kPeriodNs;
        const int64_t phaseX = static_cast<int64_t>(tick % (2 * travelX));
        const int64_t phaseY = static_cast<int64_t>(tick % (2 * travelY));
        const int x = int(phaseX < travelX ? phaseX : 2 * travelX - phaseX);
        const int y = int(phaseY < travelY ? phaseY : 2 * travelY - phaseY);
        FillRect(planes, x & ~1, y & ~1, shapeW & ~1, shapeH & ~1, 200, x & ~1, shapeW & ~1, 110,
                 150);
        DrawCounter(planes, static_cast<uint64_t>(tick));
    }
    CVPixelBufferUnlockBaseAddress(raw, 0);
    CVBufferSetAttachment(raw, kCVImageBufferYCbCrMatrixKey, kCVImageBufferYCbCrMatrix_ITU_R_709_2,
        kCVAttachmentMode_ShouldPropagate);
    CVBufferSetAttachment(raw, kCVImageBufferColorPrimariesKey,
        kCVImageBufferColorPrimaries_ITU_R_709_2, kCVAttachmentMode_ShouldPropagate);
    CVBufferSetAttachment(raw, kCVImageBufferTransferFunctionKey,
        kCVImageBufferTransferFunction_ITU_R_709_2, kCVAttachmentMode_ShouldPropagate);
    return buffer;
}
} // namespace

@implementation KMSourceStream {
    dispatch_queue_t _queue;
    CMIOExtensionStream* _stream;
    CMVideoFormatDescriptionRef _formatDescription;
    dispatch_source_t _timer;
    uint64_t _startNs;
    uint64_t _frameIndex;
    NSInteger _activeConsumers;
    BOOL _firstFrame;
}

- (instancetype)initWithQueue:(dispatch_queue_t)queue {
    if ((self = [super init])) {
        _queue = queue;
        const CMTime frameDuration = CMTimeMake(1, kFps);
        // The format description extensions must exactly match the BT.709
        // attachments MakeFrame sets on every pixel buffer, otherwise
        // CMSampleBufferCreateForImageBuffer fails with
        // kCMSampleBufferError_InvalidMediaFormat (-12743).
        NSDictionary* extensions = @{
            (__bridge NSString*)kCVImageBufferYCbCrMatrixKey :
                (__bridge NSString*)kCVImageBufferYCbCrMatrix_ITU_R_709_2,
            (__bridge NSString*)kCVImageBufferColorPrimariesKey :
                (__bridge NSString*)kCVImageBufferColorPrimaries_ITU_R_709_2,
            (__bridge NSString*)kCVImageBufferTransferFunctionKey :
                (__bridge NSString*)kCVImageBufferTransferFunction_ITU_R_709_2,
        };
        const OSStatus status = CMVideoFormatDescriptionCreate(kCFAllocatorDefault,
            kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange, kWidth, kHeight,
            (__bridge CFDictionaryRef)extensions, &_formatDescription);
        if (status != noErr) return nil;
        CMIOExtensionStreamFormat* format = [[CMIOExtensionStreamFormat alloc]
            initWithFormatDescription:_formatDescription
                       maxFrameDuration:frameDuration
                       minFrameDuration:frameDuration
                     validFrameDurations:nil];
        if (!format) return nil;
        _stream = [[CMIOExtensionStream alloc] initWithLocalizedName:@"Camera"
                                                            streamID:[[NSUUID alloc]
                                                                          initWithUUIDString:
                                                                              kKMSourceStreamIDString]
                                                           direction:CMIOExtensionStreamDirectionSource
                                                           clockType:CMIOExtensionStreamClockTypeHostTime
                                                               source:self];
        if (!_stream) return nil;
    }
    return self;
}

- (void)dealloc {
    [self stopGenerator];
    if (_formatDescription) CFRelease(_formatDescription);
}

- (CMIOExtensionStream*)stream {
    return _stream;
}

- (NSInteger)activeConsumerCount {
    return _activeConsumers;
}

#pragma mark - CMIOExtensionStreamSource

- (NSArray<CMIOExtensionStreamFormat*>*)formats {
    const CMTime frameDuration = CMTimeMake(1, kFps);
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
    const CMTime frameDuration = CMTimeMake(1, kFps);
    NSDictionary* durationDict = (__bridge_transfer NSDictionary*)
        CMTimeCopyAsDictionary(frameDuration, kCFAllocatorDefault);
    NSMutableDictionary<CMIOExtensionProperty, CMIOExtensionPropertyState*>* dict =
        [NSMutableDictionary dictionary];
    CMIOExtensionPropertyAttributes* readonly = CMIOExtensionPropertyAttributes
        .readOnlyPropertyAttribute;
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
    (void)client;
    // Stage 5: every capture app may consume the source. Producer authentication
    // applies to the sink stream in stage 6 (W6-2), not here.
    return YES;
}

- (BOOL)startStreamAndReturnError:(NSError* _Nullable*)outError {
    (void)outError;
    ++_activeConsumers;
    NSLog(@"KMSourceStream: startStream (consumers=%ld)", (long)_activeConsumers);
    [self startGenerator];
    return YES;
}

- (BOOL)stopStreamAndReturnError:(NSError* _Nullable*)outError {
    (void)outError;
    // Count unmatched calls: one consumer stopping must not stop the others.
    if (_activeConsumers > 0) --_activeConsumers;
    NSLog(@"KMSourceStream: stopStream (consumers=%ld)", (long)_activeConsumers);
    if (_activeConsumers == 0) [self stopGenerator];
    return YES;
}

#pragma mark - generator

- (void)startGenerator {
    if (_timer) return;
    const uint64_t now = HostNanos();
    NSLog(@"KMSourceStream: generator starting (now=%llu)", now);
    if (_startNs == 0 || now < _startNs) _startNs = now;
    // Align to the absolute schedule so PTS stays monotonic across restarts.
    _frameIndex = (now - _startNs) / kPeriodNs + 1;
    _firstFrame = YES;
    _timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, _queue);
    if (!_timer) return;
    __weak KMSourceStream* weakSelf = self;
    dispatch_source_set_event_handler(_timer, ^{
        [weakSelf tick];
    });
    const uint64_t target = _startNs + _frameIndex * kPeriodNs;
    const uint64_t next = target > now ? target - now : 1;
    dispatch_source_set_timer(_timer, dispatch_time(DISPATCH_TIME_NOW, int64_t(next)),
                              DISPATCH_TIME_FOREVER, 500000);
    dispatch_resume(_timer);
}

- (void)stopGenerator {
    if (!_timer) return;
    NSLog(@"KMSourceStream: generator stopping");
    dispatch_source_cancel(_timer);
    _timer = nil;
}

- (void)tick {
    if (!_timer) return;
    uint64_t now = HostNanos();
    uint64_t target = _startNs + _frameIndex * kPeriodNs;
    CMIOExtensionStreamDiscontinuityFlags flags = 0;
    if (_firstFrame) {
        flags |= CMIOExtensionStreamDiscontinuityFlagUnknown;
        _firstFrame = NO;
    }
    if (now > target + kPeriodNs) {
        // Fell behind: jump the schedule forward instead of bursting, and mark
        // the drop so clients do not treat the gap as continuous output.
        _frameIndex += (now - target) / kPeriodNs;
        target = _startNs + _frameIndex * kPeriodNs;
        flags |= CMIOExtensionStreamDiscontinuityFlagSampleDropped;
    }
    std::string error;
    auto image = MakeFrame(error);
    if (!image) {
        NSLog(@"KMSourceStream: MakeFrame failed: %s", error.c_str());
    }
    if (image) {
        CMSampleTimingInfo timing = {
            CMTimeMake(1, kFps),
            CMTimeMake(int64_t(target), 1000000000),
            kCMTimeInvalid,
        };
        CMSampleBufferRef sample = nullptr;
        const OSStatus sampleStatus =
            CMSampleBufferCreateForImageBuffer(kCFAllocatorDefault, image.get(), true, nullptr,
                nullptr, _formatDescription, &timing, &sample);
        if (sampleStatus == noErr && sample) {
            if (_frameIndex == 1 || _frameIndex % 150 == 0) {
                NSLog(@"KMSourceStream: send frame %llu pts=%lld flags=%u",
                      static_cast<unsigned long long>(_frameIndex),
                      static_cast<long long>(target), static_cast<unsigned>(flags));
            }
            [_stream sendSampleBuffer:sample
                        discontinuity:flags
                hostTimeInNanoseconds:target];
            CFRelease(sample);
        } else {
            NSLog(@"KMSourceStream: CMSampleBufferCreateForImageBuffer failed status=%d",
                  static_cast<int>(sampleStatus));
            if (sample) CFRelease(sample);
        }
    } else {
        flags |= CMIOExtensionStreamDiscontinuityFlagSampleDropped;
    }
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

@end
