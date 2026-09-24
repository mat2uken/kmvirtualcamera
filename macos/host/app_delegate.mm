#import "app_delegate.h"
#import "generated_video.h"
#import "host_exec_publisher.h"
#import "../receiver/cmio_sink_publisher.h"
#import "../receiver/url_session_transport.h"
#import <CoreGraphics/CoreGraphics.h>
#import <CoreMediaIO/CoreMediaIO.h>
#include "receiver/signaling/signaling_worker.h"
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "../../windows/third_party/qr/qrcodegen.hpp"
// Development identifier; must match PRODUCT_BUNDLE_IDENTIFIER of the extension target.
static NSString* const kKMExtensionIdentifier = @"com.mat2uken.kmvirtualcamera.camera-extension";

namespace {
constexpr int32_t kFps = 30;
constexpr int64_t kPeriodNs = 1000000000 / kFps;

uint64_t HostNanos() {
    const CMTime host = CMClockGetTime(CMClockGetHostTimeClock());
    const CMTime ns = CMTimeConvertScale(host, 1000000000, kCMTimeRoundingMethod_Default);
    return ns.value > 0 ? static_cast<uint64_t>(ns.value) : 0;
}
} // namespace

// Signaling phase → Japanese status text; mirrors the Windows status strings.
static NSString* KMPhaseText(km::signaling::SignalingPhase phase, NSString* detail) {
    switch (phase) {
        case km::signaling::SignalingPhase::CreatingSession: return @"セッション作成中…";
        case km::signaling::SignalingPhase::SessionCreated: return @"セッション作成完了";
        case km::signaling::SignalingPhase::WaitingForOffer:
            return @"Offer待機中 — QRを読み取り、ブラウザで送信を開始してください";
        case km::signaling::SignalingPhase::GeneratingAnswer: return @"Offer受信・Answer生成中…";
        case km::signaling::SignalingPhase::SendingAnswer: return @"Answer送信中…";
        case km::signaling::SignalingPhase::Succeeded: return @"シグナリング完了（RTC接続は未実装）";
        case km::signaling::SignalingPhase::TimedOut: return @"接続待機がタイムアウトしました";
        case km::signaling::SignalingPhase::Failed:
            if ([detail isEqualToString:@"answer generation failed"])
                return @"失敗: Answer生成不可（Mac RTC未実装: libdatachannel未導入）";
            return detail.length ? [NSString stringWithFormat:@"失敗: %@", detail] : @"失敗";
        case km::signaling::SignalingPhase::Canceled: return @"中断しました";
    }
    return @"不明";
}

// Join QR with the same generator and parameters as the Windows view
// (qrcodegen, Ecc::MEDIUM, quiet zone 4), fitted into `box` pixels.
// Returns nil on encode/render failure (Windows shows QRコード未生成 then).
static NSImage* KMJoinQrImage(NSString* text, CGFloat box) {
    if (text.length == 0) return nil;
    try {
        const qrcodegen::QrCode qr =
            qrcodegen::QrCode::encodeText(text.UTF8String, qrcodegen::QrCode::Ecc::MEDIUM);
        const int border = 4;
        const int size = qr.getSize();
        const int total = size + border * 2;
        int cell = int(box) / total;
        if (cell < 1) cell = 1;
        const int px = cell * total;
        std::vector<uint8_t> pixels(size_t(px) * size_t(px), 0xFF); // white background
        CGColorSpaceRef gray = CGColorSpaceCreateDeviceGray();
        CGContextRef ctx =
            CGBitmapContextCreate(pixels.data(), px, px, 8, px, gray, kCGImageAlphaNone);
        CGColorSpaceRelease(gray);
        if (!ctx) return nil;
        CGContextSetGrayFillColor(ctx, 0.0, 1.0);
        for (int y = 0; y < size; ++y) { // y=0 is the top row; CG origin is bottom-left
            for (int x = 0; x < size; ++x) {
                if (!qr.getModule(x, y)) continue;
                CGContextFillRect(ctx, CGRectMake((border + x) * cell,
                    (total - border - y - 1) * cell, cell, cell));
            }
        }
        CGImageRef cgImage = CGBitmapContextCreateImage(ctx);
        CGContextRelease(ctx);
        if (!cgImage) return nil;
        NSImage* image = [[NSImage alloc] initWithCGImage:cgImage size:NSMakeSize(px, px)];
        CGImageRelease(cgImage);
        return image;
    } catch (const std::exception&) {
        return nil;
    }
}

// Lightweight device re-enumeration via CoreMediaIO. Display names only: identity checks
// by display name are never sufficient (see stage 5/6 for stable IDs and directions).
// The root CMIOSystemObject is kCMIOObjectSystemObject; there is no
// CMIOHardwareSystemGetProperty entry point in the real SDK - all queries go through
// CMIOObjectGetPropertyData with a CMIOObjectPropertyAddress.
static NSArray<NSString*>* KMEnumerateCameraDeviceNames(void) {
    NSMutableArray<NSString*>* names = [NSMutableArray array];
    const CMIOObjectPropertyAddress devicesAddress = {
        kCMIOHardwarePropertyDevices, kCMIOObjectPropertyScopeGlobal,
        kCMIOObjectPropertyElementWildcard,
    };
    UInt32 devicesSize = 0;
    if (CMIOObjectGetPropertyDataSize(kCMIOObjectSystemObject, &devicesAddress, 0, nullptr,
            &devicesSize) != noErr ||
        devicesSize == 0 || devicesSize > 4096)
        return names;
    std::vector<CMIODeviceID> devices(devicesSize / sizeof(CMIODeviceID));
    UInt32 used = 0;
    if (CMIOObjectGetPropertyData(kCMIOObjectSystemObject, &devicesAddress, 0, nullptr,
            devicesSize, &used, devices.data()) != noErr)
        return names;
    devices.resize(used / sizeof(CMIODeviceID));

    const CMIOObjectPropertyAddress nameAddress = {
        kCMIOObjectPropertyName, kCMIOObjectPropertyScopeGlobal,
        kCMIOObjectPropertyElementWildcard,
    };
    for (CMIODeviceID device : devices) {
        CFStringRef name = nullptr;
        UInt32 nameSize = sizeof(name);
        if (CMIOObjectGetPropertyData(device, &nameAddress, 0, nullptr, nameSize, &nameSize, &name) ==
                noErr &&
            name) {
            [names addObject:(__bridge NSString*)name];
            CFRelease(name);
        }
    }
    return names;
}

@implementation KMAppDelegate {
    NSWindow* _window;
    NSTextField* _stateLabel;
    NSTextField* _detailLabel;
    NSTextField* _devicesLabel;
    NSTextField* _sinkLabel;
    // M4-a join session: signaling worker wiring (worker-thread-only blocking).
    NSTextField* _signalingUrlField;
    NSTextField* _phaseLabel;
    NSTextField* _joinUrlLabel;
    NSImageView* _qrView;
    NSUInteger _signalingGeneration; // stale callbacks from a stopped run are dropped
    // Declared before the worker so it outlives it (the worker borrows the transport;
    // transports are single-cancel, so every run gets a fresh one).
    std::unique_ptr<km::IHttpTransport> _signalingTransport;
    std::unique_ptr<km::signaling::SignalingWorker> _signalingWorker;
    KMCMSinkPublisher* _publisher;
    dispatch_queue_t _genQueue;
    dispatch_source_t _genTimer;
    uint64_t _genStartNs;
    uint64_t _genIndex;
}

- (KMExtensionManager*)extensionManager {
    static KMExtensionManager* manager = nil;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ manager = [[KMExtensionManager alloc] init]; });
    return manager;
}

- (NSTextField*)makeLabelWithFrame:(NSRect)frame bold:(BOOL)bold {
    NSTextField* label = [[NSTextField alloc] initWithFrame:frame];
    label.editable = NO;
    label.bezeled = NO;
    label.drawsBackground = NO;
    label.lineBreakMode = NSLineBreakByWordWrapping;
    label.maximumNumberOfLines = 0;
    if (bold) label.font = [NSFont boldSystemFontOfSize:13];
    return label;
}

- (void)refreshDevices {
    NSArray<NSString*>* names = KMEnumerateCameraDeviceNames();
    _devicesLabel.stringValue =
        [NSString stringWithFormat:@"カメラデバイス (%lu): %@", (unsigned long)names.count,
            names.count ? [names componentsJoinedByString:@" / "] : @"(なし)"];
}

// States from KMExtensionManager are kept distinct in the UI; request_completed alone
// never means "camera usable" - devices are re-enumerated afterwards.
- (void)reportState:(NSString*)state detail:(NSString*)detail {
    static NSDictionary<NSString*, NSString*>* human;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        human = @{
            @"activating": @"有効化要求を送信中",
            @"approval_required": @"承認待ち（システム設定 > 一般 > ログイン項目と拡張機能）",
            @"reboot_required": @"再起動が必要です",
            @"request_completed": @"要求完了 — 下でdeviceを再列挙します",
            @"request_failed": @"要求失敗",
            @"deactivating": @"無効化要求を送信中",
        };
    });
    _stateLabel.stringValue = [NSString stringWithFormat:@"状態: %@ (%@)",
        human[state] ?: @"不明", state];
    _detailLabel.stringValue = detail.length ? detail : @"";
    if ([state isEqualToString:@"request_completed"]) [self refreshDevices];
}

- (void)activate:(id)sender {
    (void)sender;
    [self.extensionManager activateIdentifier:kKMExtensionIdentifier];
}

- (void)deactivate:(id)sender {
    (void)sender;
    [self.extensionManager deactivateIdentifier:kKMExtensionIdentifier];
}

#pragma mark - join session (M4-a)

// Cancel + join the worker and drop its transport. Called on start (restart),
// stop and termination; also bumps the generation so callbacks already
// dispatched by the old run cannot overwrite the new state.
- (void)stopSignalingWorker {
    if (_signalingWorker) {
        _signalingWorker->cancel(); // IHttpTransport::cancel aborts the in-flight call, then join
        _signalingWorker.reset();
    }
    _signalingTransport.reset();
    ++_signalingGeneration;
}

- (void)startSignaling:(id)sender {
    (void)sender;
    [self stopSignalingWorker];
    const NSUInteger generation = _signalingGeneration;
    // The callbacks are released with the worker, so the strong self capture
    // cannot outlive stopSignalingWorker; UI updates jump to the main queue.
    KMAppDelegate* strongSelf = self;
    km::signaling::SignalingWorker::Callbacks callbacks;
    callbacks.onPhase = [strongSelf, generation](km::signaling::SignalingPhase phase,
                                                  const std::string& detail) {
        NSLog(@"KMAppDelegate: signaling phase=%d detail=%s", int(phase), detail.c_str());
        NSString* text = KMPhaseText(phase, [NSString stringWithUTF8String:detail.c_str()] ?: @"");
        dispatch_async(dispatch_get_main_queue(), ^{
            if (strongSelf->_signalingGeneration != generation) return;
            strongSelf->_phaseLabel.stringValue = text;
        });
    };
    callbacks.onSessionCreated = [strongSelf, generation](const km::signaling::CreateSessionResponse& session) {
        NSString* joinUrl = [NSString stringWithUTF8String:session.joinUrl.c_str()] ?: @"";
        NSLog(@"KMAppDelegate: join URL ready: %@", joinUrl);
        dispatch_async(dispatch_get_main_queue(), ^{
            if (strongSelf->_signalingGeneration != generation) return;
            strongSelf->_joinUrlLabel.stringValue = joinUrl;
            strongSelf->_qrView.image = KMJoinQrImage(joinUrl, 130);
        });
    };
    // Mac RTC is not wired yet (libdatachannel dependency undecided): fail the
    // run with the explicit Answer reason instead of fabricating an SDP answer.
    callbacks.makeAnswer = [](const std::string&, const km::signaling::CreateSessionResponse&)
        -> std::optional<std::string> { return std::nullopt; };
    const std::string url = _signalingUrlField.stringValue.UTF8String ?: "";
    try {
        _signalingTransport = km::mac::MakeUrlSessionTransport();
        _signalingWorker = std::make_unique<km::signaling::SignalingWorker>(
            *_signalingTransport, url, std::move(callbacks));
        if (!_signalingWorker->start("macos-receiver")) {
            _phaseLabel.stringValue = @"状態: 失敗 — ワーカースレッドを起動できません";
            [self stopSignalingWorker];
            return;
        }
        _phaseLabel.stringValue = @"状態: セッション作成中…";
    } catch (const std::exception& e) {
        _phaseLabel.stringValue = [NSString stringWithFormat:@"状態: 失敗 — %s", e.what()];
        [self stopSignalingWorker];
    }
}

- (void)sessionStop:(id)sender {
    (void)sender;
    [self stopSignalingWorker];
    _phaseLabel.stringValue = @"状態: 停止しました";
    _joinUrlLabel.stringValue = @"join URL: (未作成)";
    _qrView.image = nil;
}

#pragma mark - sink publisher (stage 6)

static NSString* KMSinkStateText(KMSinkPublisherState state) {
    switch (state) {
        case KMSinkPublisherStateStopped: return @"停止中";
        case KMSinkPublisherStateUnavailable:
            return @"未検出（Extension列挙を2秒間隔で再試行中）";
        case KMSinkPublisherStateOpening: return @"開始中";
        case KMSinkPublisherStateReady: return @"投入中";
        case KMSinkPublisherStateBackpressure: return @"背圧（queue満杯で新規frameをdrop）";
        case KMSinkPublisherStateDisconnected: return @"切断（再列挙・再接続試行中）";
    }
    return @"不明";
}

- (void)sinkStart:(id)sender {
    (void)sender;
    // Refresh the mode-C evidence so the extension's recorded path matches this
    // instance before the first StartStream attempt.
    KMHostPublishExecutableEvidence();
    [_publisher start];
    [self startGenerator];
    _sinkLabel.stringValue = @"sink投入: 開始要求を送信";
}

- (void)sinkStop:(id)sender {
    (void)sender;
    [_publisher stop];
    [self stopGenerator];
    _sinkLabel.stringValue = @"sink投入: 停止中";
}

// W6-4: fill the finite sink queue to observe backpressure and newest-frame
// replacement when the burst ends.
- (void)sinkBurst:(id)sender {
    (void)sender;
    [_publisher runFeedBurstForNanoseconds:2000000000ull];
}

// The known moving video runs from its own 30fps absolute timer on a serial queue
// and only hands frames to the publisher's newest-wins slot (never blocks on IO).
// create/cancel/tick all serialize on _genQueue: generateTick re-arms its own
// timer at the end of every tick, so cancelling and nil'ing the ivar from
// another thread let a concurrent tick pass the nil check and then call
// dispatch_source_set_timer on a released source (observed SIGSEGV in
// generateTick 10ms after stopGenerator ran on the main thread).
- (dispatch_queue_t)genQueue {
    if (!_genQueue) _genQueue = dispatch_queue_create("jp.km.hostgenerator", DISPATCH_QUEUE_SERIAL);
    return _genQueue;
}

- (void)startGenerator {
    dispatch_async([self genQueue], ^{
        if (self->_genTimer) return;
        const uint64_t now = HostNanos();
        if (self->_genStartNs == 0 || now < self->_genStartNs) self->_genStartNs = now;
        self->_genIndex = (now - self->_genStartNs) / kPeriodNs + 1;
        self->_genTimer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, self->_genQueue);
        if (!self->_genTimer) return;
        __weak KMAppDelegate* weakSelf = self;
        dispatch_source_set_event_handler(self->_genTimer, ^{
            [weakSelf generateTick];
        });
        const uint64_t target = self->_genStartNs + self->_genIndex * kPeriodNs;
        dispatch_source_set_timer(self->_genTimer, dispatch_time(DISPATCH_TIME_NOW,
                                                           int64_t(target > now ? target - now : 1)),
                                  DISPATCH_TIME_FOREVER, 500000);
        dispatch_resume(self->_genTimer);
        NSLog(@"KMAppDelegate: generator starting (index=%llu)", self->_genIndex);
    });
}

- (void)stopGenerator {
    dispatch_async([self genQueue], ^{
        if (!self->_genTimer) return;
        NSLog(@"KMAppDelegate: generator stopping");
        dispatch_source_cancel(self->_genTimer);
        self->_genTimer = nil;
    });
}

- (void)generateTick {
    if (!_genTimer) return;
    uint64_t now = HostNanos();
    uint64_t target = _genStartNs + _genIndex * kPeriodNs;
    if (now > target + kPeriodNs) {
        _genIndex += (now - target) / kPeriodNs;
        target = _genStartNs + _genIndex * kPeriodNs;
    }
    std::string error;
    km::mac::PixelBuffer frame = km::host::MakeGeneratedFrame(_genIndex, error);
    if (frame) {
        [_publisher publishPixelBuffer:frame.get()];
        if (_genIndex == 1 || _genIndex % 150 == 0)
            NSLog(@"KMAppDelegate: generate frame=%llu target=%llu", _genIndex, target);
    } else {
        NSLog(@"KMAppDelegate: MakeGeneratedFrame failed: %s", error.c_str());
    }
    ++_genIndex;
    now = HostNanos();
    uint64_t nextTarget = _genStartNs + _genIndex * kPeriodNs;
    if (nextTarget <= now) {
        _genIndex += (now - nextTarget) / kPeriodNs + 1;
        nextTarget = _genStartNs + _genIndex * kPeriodNs;
    }
    dispatch_source_set_timer(_genTimer, dispatch_time(DISPATCH_TIME_NOW,
                                                       int64_t(nextTarget - now)),
                              DISPATCH_TIME_FOREVER, 500000);
}

#pragma mark - lifecycle

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
    (void)notification;
    const NSRect frame = NSMakeRect(0, 0, 560, 680);
    _window = [[NSWindow alloc] initWithContentRect:frame
        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable
        backing:NSBackingStoreBuffered defer:NO];
    _window.title = @"KMVirtualCamera Host";
    NSView* content = _window.contentView;

    _stateLabel = [self makeLabelWithFrame:NSMakeRect(20, 364, 520, 24) bold:YES];
    _stateLabel.stringValue = @"状態: 待機中";
    _detailLabel = [self makeLabelWithFrame:NSMakeRect(20, 328, 520, 36) bold:NO];
    _devicesLabel = [self makeLabelWithFrame:NSMakeRect(20, 292, 520, 36) bold:NO];
    _devicesLabel.stringValue = @"カメラデバイス: 未取得";

    NSButton* activate = [NSButton buttonWithTitle:@"有効化" target:self action:@selector(activate:)];
    activate.frame = NSMakeRect(20, 248, 120, 32);
    NSButton* deactivate = [NSButton buttonWithTitle:@"無効化" target:self action:@selector(deactivate:)];
    deactivate.frame = NSMakeRect(150, 248, 120, 32);
    NSButton* refresh = [NSButton buttonWithTitle:@"デバイス再取得" target:self action:@selector(refreshDevices)];
    refresh.frame = NSMakeRect(280, 248, 140, 32);

    _sinkLabel = [self makeLabelWithFrame:NSMakeRect(20, 206, 520, 36) bold:YES];
    _sinkLabel.stringValue = @"sink投入: 停止中";
    NSButton* sinkStart = [NSButton buttonWithTitle:@"投入開始" target:self action:@selector(sinkStart:)];
    sinkStart.frame = NSMakeRect(20, 162, 120, 32);
    NSButton* sinkStop = [NSButton buttonWithTitle:@"投入停止" target:self action:@selector(sinkStop:)];
    sinkStop.frame = NSMakeRect(150, 162, 120, 32);
    NSButton* burst = [NSButton buttonWithTitle:@"背圧試験(2s)" target:self action:@selector(sinkBurst:)];
    burst.frame = NSMakeRect(280, 162, 130, 32);

    // M4-a join session UI: URL entry, start/stop, phase, join URL + QR.
    NSTextField* joinTitle = [self makeLabelWithFrame:NSMakeRect(20, 630, 520, 24) bold:YES];
    joinTitle.stringValue = @"受信セッション（ブラウザから送信）";
    NSTextField* urlTitle = [self makeLabelWithFrame:NSMakeRect(20, 602, 96, 24) bold:NO];
    urlTitle.stringValue = @"Signaling URL";
    _signalingUrlField = [[NSTextField alloc] initWithFrame:NSMakeRect(120, 599, 300, 24)];
    _signalingUrlField.stringValue = @"http://127.0.0.1:8787"; // local dev default, as on Windows
    NSButton* sessionStart = [NSButton buttonWithTitle:@"セッション開始"
                                                target:self
                                                action:@selector(startSignaling:)];
    sessionStart.frame = NSMakeRect(20, 558, 130, 32);
    NSButton* sessionStop = [NSButton buttonWithTitle:@"停止"
                                               target:self
                                               action:@selector(sessionStop:)];
    sessionStop.frame = NSMakeRect(160, 558, 90, 32);
    _phaseLabel = [self makeLabelWithFrame:NSMakeRect(20, 520, 380, 44) bold:YES];
    _phaseLabel.stringValue = @"状態: 待機中";
    _joinUrlLabel = [self makeLabelWithFrame:NSMakeRect(20, 468, 380, 48) bold:NO];
    _joinUrlLabel.selectable = YES;
    _joinUrlLabel.stringValue = @"join URL: (未作成)";
    _qrView = [[NSImageView alloc] initWithFrame:NSMakeRect(410, 436, 130, 130)];
    _qrView.imageScaling = NSImageScaleProportionallyUpOrDown;

    NSTextField* note = [self makeLabelWithFrame:NSMakeRect(20, 20, 520, 130) bold:NO];
    note.stringValue = @"開発用host（段階4–6）: 有効化要求の状態表示、device再列挙、"
                        "sink投入（720p30の既知映像をCamera Extensionのsinkへ送出）を行います。"
                        "投入中はsourceから一般アプリで撮影できます。Extension識別子・"
                        "producer要求文は開発用の暫定値です。";

    for (NSView* view in @[ _stateLabel, _detailLabel, _devicesLabel, activate, deactivate,
             refresh, _sinkLabel, sinkStart, sinkStop, burst, joinTitle, urlTitle,
             _signalingUrlField, sessionStart, sessionStop, _phaseLabel, _joinUrlLabel,
             _qrView, note ])
        [content addSubview:view];

    __weak KMAppDelegate* weakSelf = self;
    self.extensionManager.statusHandler = ^(NSString* state, NSString* detail) {
        dispatch_async(dispatch_get_main_queue(), ^{ [weakSelf reportState:state detail:detail]; });
    };

    _publisher = [[KMCMSinkPublisher alloc] init];
    _publisher.stateHandler = ^(KMSinkPublisherState state, NSString* detail) {
        KMAppDelegate* strongSelf = weakSelf;
        if (!strongSelf) return;
        strongSelf->_sinkLabel.stringValue =
            [NSString stringWithFormat:@"sink投入: %@\n%@", KMSinkStateText(state), detail];
    };

    // Mode-C evidence must exist before any StartStream attempt (launch-time
    // publish; sinkStart: refreshes it).
    KMHostPublishExecutableEvidence();

    [self refreshDevices];
    [_window center];
    [_window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];

    // Dev smoke only: -KMStartSignaling starts a session without UI scripting so
    // the phase flow lands in the launch log.
    if ([[[NSProcessInfo processInfo] arguments] containsObject:@"-KMStartSignaling"]) {
        dispatch_async(dispatch_get_main_queue(), ^{ [self startSignaling:nil]; });
    }
}

- (void)applicationWillTerminate:(NSNotification*)notification {
    (void)notification;
    [self stopSignalingWorker];
    [self stopGenerator];
    [_publisher stop];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender {
    (void)sender;
    return YES;
}
@end
