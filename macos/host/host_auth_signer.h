#pragma once
#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN
// Stage 6 (W6-2, mode K) host side: the producer authentication key pair lives
// in THIS app's login keychain; only the PUBLIC half is embedded in the camera
// extension at build time (scripts/provision_macos_auth_key.sh writes
// camera-extension/producer_auth_pubkey.h). The private key's keychain ACL is
// this app's designated requirement, which securityd evaluates against the LIVE
// code signature at every signing operation - the OS-verified signature check
// W6-2 requires. No key material is ever written into the repo or the logs.

// Finds (or creates, on first run) the producer auth key and returns its public
// key as a raw X9.63 point (65 bytes). Idempotent: re-running re-exports the
// same key unless rotate is YES (which deletes the existing key first).
// Returns nil and sets *statusOut to the failing status on error.
NSData* _Nullable KMHostProducerAuthPublicKey(BOOL rotate, int* statusOut);

// Signs (challenge || this process's pid || unix seconds) with the producer auth
// key and returns the response blob for the extension's response property:
// [0,8) little-endian int64 timestamp, [8,...) ECDSA-SHA256 (X9.62 DER)
// signature. Returns nil and sets *statusOut when the key is missing or the
// signature fails (logged as integers only - no key material in logs).
NSData* _Nullable KMHostSignProducerAuth(NSData* challenge, int* statusOut);
NS_ASSUME_NONNULL_END
