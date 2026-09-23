#import "extension_manager.h"

@implementation KMExtensionManager {
    OSSystemExtensionRequest* _request;
}
- (void)report:(NSString*)state detail:(NSString*)detail {
    if (self.statusHandler) self.statusHandler(state, detail);
}
- (void)activateIdentifier:(NSString*)identifier {
    NSAssert([NSThread isMainThread], @"Call on the main thread");
    if (_request || identifier.length == 0) return;
    _request = [OSSystemExtensionRequest activationRequestForExtension:identifier queue:dispatch_get_main_queue()];
    _request.delegate = self;
    [self report:@"activating" detail:@"Activation submitted; this is not a streaming state"];
    [OSSystemExtensionManager.sharedManager submitRequest:_request];
}
- (void)deactivateIdentifier:(NSString*)identifier {
    NSAssert([NSThread isMainThread], @"Call on the main thread");
    if (_request || identifier.length == 0) return;
    _request = [OSSystemExtensionRequest deactivationRequestForExtension:identifier queue:dispatch_get_main_queue()];
    _request.delegate = self;
    [self report:@"deactivating" detail:@"Deactivation submitted"];
    [OSSystemExtensionManager.sharedManager submitRequest:_request];
}
- (void)requestNeedsUserApproval:(OSSystemExtensionRequest*)request {
    if (request == _request) [self report:@"approval_required" detail:@"User approval is required in System Settings"];
}
- (OSSystemExtensionReplacementAction)request:(OSSystemExtensionRequest*)request
                 actionForReplacingExtension:(OSSystemExtensionProperties*)existing
                               withExtension:(OSSystemExtensionProperties*)replacement {
    // OS validates signing requirements. Never accept an unexpected extension identifier.
    if (request != _request || ![existing.bundleIdentifier isEqualToString:replacement.bundleIdentifier])
        return OSSystemExtensionReplacementActionCancel;
    return OSSystemExtensionReplacementActionReplace;
}
- (void)request:(OSSystemExtensionRequest*)request didFinishWithResult:(OSSystemExtensionRequestResult)result {
    if (request != _request) return;
    _request = nil;
    [self report:(result == OSSystemExtensionRequestWillCompleteAfterReboot ? @"reboot_required" : @"request_completed")
          detail:@"Re-enumerate the camera before reporting availability"];
}
- (void)request:(OSSystemExtensionRequest*)request didFailWithError:(NSError*)error {
    if (request != _request) return;
    _request = nil;
    [self report:@"request_failed" detail:error.localizedDescription];
}
@end
