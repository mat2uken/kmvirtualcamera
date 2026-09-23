#pragma once
#import <AppKit/AppKit.h>
#import "extension_manager.h"

NS_ASSUME_NONNULL_BEGIN
// Programmatic AppKit host: activation states, device re-enumeration, quit handling.
@interface KMAppDelegate : NSObject <NSApplicationDelegate>
@property(nonatomic, readonly) KMExtensionManager* extensionManager;
@end
NS_ASSUME_NONNULL_END
