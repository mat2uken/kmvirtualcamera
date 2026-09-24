#pragma once
#import <Foundation/Foundation.h>
#import <CoreMediaIO/CMIOExtension.h>

NS_ASSUME_NONNULL_BEGIN
// Stage 6 (W6-2): decides whether a connecting client may act as the sink producer.
// OS-provided facts must all hold. Modes, tried in order:
//   K. in-band key attestation (tried first; see below), then the file-based
//      ladder A/B/B'/C as fallback when the attestation channel yields nothing:
//   A. client.pid resolves to a live process code object
//      (SecCodeCopyGuestWithAttributes) and that live code satisfies
//      kKMHostProducerRequirement (SecCodeCheckValidity) - kernel-bound, no TOCTOU;
//      the extension's App Sandbox denies the cross-process lookup (measured
//      status 100001 = EPERM), and Apple System Policy even requires the sandbox
//      for this extension type (sandbox-less builds are exec-denied by the
//      kernel), so B/C are used instead:
//   B. static check of the client's live proc_pidpath file, and
//   B'. when Security's own open of that path is denied (measured EPERM), a
//      plain byte read of the SAME live path staged into this process's own
//      group container and statically checked there - the path binding stays
//      inherent because the bytes are read from proc_pidpath's result, and
//      authorization still rests only on the OS-verified signature check;
//   C. shared-container evidence: the host publishes a byte copy of its own
//      executable plus its run path into its App Group container (writing gated
//      by the App Group entitlement, AMFI-verified against the team signature);
//      the extension finds that container by probing each local user's home
//      (containers resolve per-user, and the sandbox denies learning the
//      client's uid: proc_pidinfo measured EPERM), requires the recorded path
//      to equal the pid's live proc_pidpath result, and validates the copy with
//      SecStaticCodeCheckValidity against the SAME requirement - OS-verified
//      signing of evidence bound to the running pid, never a pid / display name /
//      unsigned string alone (W6-2). Residual: modes B/B'/C bind through the live
//      path and can race an exec-swap; mode A (no window) is preferred whenever
//      the sandbox permits it.
//   4. the client's CMIO signing identifier, when CMIO provides a real value,
//      equals kKMHostSigningIdentifier (macOS 13+ cross-check; stage 6 runs showed
//      @"unknown" for validly signed processes, so an absent value is skipped and
//      authorization never rests on that string alone).
// A PID alone, a display name alone, or an unverified signing ID string alone never
// authorizes. reasonOut receives a human-readable rejection reason on failure.
API_AVAILABLE(macos(12.3))
BOOL KMVerifyHostProducer(CMIOExtensionClient* client,
                          NSString* _Nullable* _Nullable reasonOut);

// Mode K - in-band key attestation (tried before the ladder). challenge and
// response are this device's current custom property values, captured by the
// device at authorize time. Verifies an ECDSA-SHA256 signature over
// (challenge || client.pid || unix seconds) against the public key pinned at
// build time (producer_auth_pubkey.h). Security basis (W6-2): only a process
// securityd lets use the private key can produce the signature - the key's
// keychain ACL is the host's designated requirement, evaluated against its LIVE
// code signature at signing time (OS-verified). The pid inside the signed
// payload binds the attestation to THIS client, the timestamp is limited to
// kKMAuthMaxClockSkewSeconds, and the challenge is single-use (rotated when a
// response is accepted), so a captured response cannot be replayed.
// reasonOut receives a human-readable rejection reason on failure.
API_AVAILABLE(macos(12.3))
BOOL KMVerifyKeyAttestation(CMIOExtensionClient* client,
                            NSData* challenge,
                            NSData* response,
                            NSString* _Nullable* _Nullable reasonOut);
NS_ASSUME_NONNULL_END
