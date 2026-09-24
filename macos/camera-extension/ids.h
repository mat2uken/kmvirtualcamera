#pragma once
#import <Foundation/Foundation.h>

// Stable device/stream identifiers, kept in the repo so clients can re-identify
// the camera across upgrades. Never re-identify by display name alone (stage 5/6 rule).
// These are development values, not product settings.
static NSString* const kKMDeviceIDString = @"c1b67446-47cf-4d2e-9c5d-76a56127f3de";
static NSString* const kKMSourceStreamIDString = @"ab8614be-9eff-45bb-9c47-4ee598fe128f";
// Stage 6 (W6-1): the sink stream gets its own stable ID; source and sink are
// additionally separated by direction (verified on device: source=1/input,
// sink=0/output in the DAL - kCMIOStreamPropertyDirection).
static NSString* const kKMSinkStreamIDString = @"8ac04e93-75dd-4725-b372-2a51f26272ef";

// Stage 6 (W6-2): the OS-checked requirement the single sink producer must satisfy.
// identifier = host PRODUCT_BUNDLE_IDENTIFIER, certificate leaf OU = DEVELOPMENT_TEAM.
// Development placeholder values; the product identifiers are undecided (stage 8).
static NSString* const kKMHostProducerRequirement =
    @"identifier \"com.mat2uken.kmvirtualcamera\" and certificate leaf[subject.OU] = K7VNGA9K78";
// The client's signing identifier must equal this too (macOS 13+ cross-check).
static NSString* const kKMHostSigningIdentifier = @"com.mat2uken.kmvirtualcamera";

// Stage 6 (W6-2): shared App Group used by producer_auth mode C. The sandboxed
// extension cannot read the host's on-disk executable (measured EPERM), so the
// host publishes a byte copy of its own main executable plus its run path into
// this container; the extension validates the copy with SecStaticCodeCheckValidity
// and binds it to the client pid via proc_pidpath. Dev placeholder team value.
static NSString* const kKMAppGroupIdentifier =
    @"K7VNGA9K78.com.mat2uken.kmvirtualcamera";
static NSString* const kKMHostEvidenceBinaryName = @"host-executable.bin";
static NSString* const kKMHostEvidencePathName = @"host-executable.path";

// Stage 6 (W6-2, mode K): in-band challenge/attestation channel through custom
// device properties (WWDC22 bridging: extension key = 4cc_<selector>_<scope>_
// <element>; the host addresses the same property via the CoreMediaIO C API).
// The host reads the challenge, signs (challenge || its pid || unix seconds)
// with the private key held in its login keychain, writes the response, and only
// then calls startStream - the extension verifies the signature against the
// public key pinned at build time (producer_auth_pubkey.h).
static NSString* const kKMAuthChallengeProperty = @"4cc_chlg_glob_0000";
static NSString* const kKMAuthResponseProperty = @"4cc_resp_glob_0000";
// DAL-side four-character selectors; must match the selector part above.
// The element encoding of the framework is probed on the host (element 0 first,
// then '0000' as a raw four-char code) - both spellings are logged.
enum {
    kKMAuthChallengeSelector = 'chlg',
    kKMAuthResponseSelector = 'resp',
};
// Mode K payload layout: challenge = 32 random bytes; signed payload =
// challenge || pid (int32 LE) || unix seconds (int64 LE); response property =
// unix seconds (int64 LE) || ECDSA-SHA256 signature (X9.62 DER).
static const uint32_t kKMAuthChallengeLength = 32;
static const int64_t kKMAuthMaxClockSkewSeconds = 10;

// Explicit little-endian packing (both sides run on Apple Silicon today; byte
// order is written out so a future Intel build keeps the wire format stable).
static inline void KMWriteLE32(uint8_t* dst, uint32_t value) {
    for (int i = 0; i < 4; ++i) dst[i] = (uint8_t)((value >> (8 * i)) & 0xFF);
}
static inline void KMWriteLE64(uint8_t* dst, uint64_t value) {
    for (int i = 0; i < 8; ++i) dst[i] = (uint8_t)((value >> (8 * i)) & 0xFF);
}
static inline uint64_t KMReadLE64(const uint8_t* src) {
    uint64_t value = 0;
    for (int i = 0; i < 8; ++i) value |= ((uint64_t)src[i]) << (8 * i);
    return value;
}
