#import "app_delegate.h"
#import <CoreMediaIO/CoreMediaIO.h>
#include <vector>
// Development identifier; must match PRODUCT_BUNDLE_IDENTIFIER of the extension target.
static NSString* const kKMExtensionIdentifier = @"com.mat2uken.kmvirtualcamera.camera-extension";

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
    if (CMIOObjectGetPropertyData(kCMIOObjectSystemObject, &devicesAddress, 0, nullptr, devicesSize,
            &used, devices.data()) != noErr)
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

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
    (void)notification;
    const NSRect frame = NSMakeRect(0, 0, 560, 300);
    _window = [[NSWindow alloc] initWithContentRect:frame
        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable
        backing:NSBackingStoreBuffered defer:NO];
    _window.title = @"KMVirtualCamera Host";
    NSView* content = _window.contentView;

    _stateLabel = [self makeLabelWithFrame:NSMakeRect(20, 244, 520, 24) bold:YES];
    _stateLabel.stringValue = @"状態: 待機中";
    _detailLabel = [self makeLabelWithFrame:NSMakeRect(20, 208, 520, 36) bold:NO];
    _devicesLabel = [self makeLabelWithFrame:NSMakeRect(20, 172, 520, 36) bold:NO];
    _devicesLabel.stringValue = @"カメラデバイス: 未取得";

    NSButton* activate = [NSButton buttonWithTitle:@"有効化" target:self action:@selector(activate:)];
    activate.frame = NSMakeRect(20, 128, 120, 32);
    NSButton* deactivate = [NSButton buttonWithTitle:@"無効化" target:self action:@selector(deactivate:)];
    deactivate.frame = NSMakeRect(150, 128, 120, 32);
    NSButton* refresh = [NSButton buttonWithTitle:@"デバイス再取得" target:self action:@selector(refreshDevices)];
    refresh.frame = NSMakeRect(280, 128, 140, 32);

    NSTextField* note = [self makeLabelWithFrame:NSMakeRect(20, 20, 520, 96) bold:NO];
    note.stringValue = @"開発用host（段階4）: 有効化要求の状態表示とdevice再列挙のみを行います。"
                        "生成映像のcapture確認は段階5、sink投入は段階6で行います。"
                        "Extension識別子は開発用の暫定値です。";

    for (NSView* view in @[ _stateLabel, _detailLabel, _devicesLabel, activate, deactivate, refresh, note ])
        [content addSubview:view];

    __weak KMAppDelegate* weakSelf = self;
    self.extensionManager.statusHandler = ^(NSString* state, NSString* detail) {
        dispatch_async(dispatch_get_main_queue(), ^{ [weakSelf reportState:state detail:detail]; });
    };
    [self refreshDevices];
    [_window center];
    [_window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender {
    (void)sender;
    return YES;
}
@end
