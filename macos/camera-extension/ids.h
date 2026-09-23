#pragma once
#import <Foundation/Foundation.h>

// Stable device/stream identifiers, kept in the repo so clients can re-identify
// the camera across upgrades. Never re-identify by display name alone (stage 5/6 rule).
// These are development values, not product settings.
static NSString* const kKMDeviceIDString = @"c1b67446-47cf-4d2e-9c5d-76a56127f3de";
static NSString* const kKMSourceStreamIDString = @"ab8614be-9eff-45bb-9c47-4ee598fe128f";
