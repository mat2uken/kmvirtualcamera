#pragma once
#import <Foundation/Foundation.h>
#import <SystemExtensions/SystemExtensions.h>

NS_ASSUME_NONNULL_BEGIN
// Must be retained by the app. All methods and notifications run on the main queue.
@interface KMExtensionManager : NSObject <OSSystemExtensionRequestDelegate>
@property(nonatomic, copy, nullable) void (^statusHandler)(NSString* state, NSString* detail);
- (void)activateIdentifier:(NSString*)identifier;
- (void)deactivateIdentifier:(NSString*)identifier;
@end
NS_ASSUME_NONNULL_END
