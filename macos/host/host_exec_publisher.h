#pragma once
#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN
// Stage 6 (W6-2, producer_auth mode C): publishes a byte copy of this app's own
// main executable into the shared App Group container together with the absolute
// path the app is running from. The camera extension's sandbox cannot read
// /Applications/... directly (measured EPERM on SecStaticCodeCreateWithPath), so
// it validates this copy instead: Security checks the copy's code signature
// against kKMHostProducerRequirement (OS-verified), and the recorded path must
// equal the extension's own proc_pidpath result for the connecting pid (kernel
// path binding). Writing the container requires the App Group entitlement, which
// AMFI validates against this app's team signature - only our team's code can
// publish evidence. Called at launch and on every sink-start request.
void KMHostPublishExecutableEvidence(void);
NS_ASSUME_NONNULL_END
