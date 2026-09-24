#import "producer_auth.h"
#import "ids.h"
#import "producer_auth_pubkey.h"
#import <Security/Security.h>
#import <libproc.h>
#import <pwd.h>
#import <errno.h>
#include <time.h>

// Plain function (not a block): assigning through an autoreleasing out-parameter
// inside a block triggers -Wblock-capture-autoreleasing.
static BOOL KMReject(NSString* _Nullable* _Nullable reasonOut, NSString* reason) {
    if (reasonOut) *reasonOut = reason;
    return NO;
}

// Measured on this system: errSecErrnoBase(100000) + EPERM(1) from the extension's
// App Sandbox denying cross-process lookups and foreign file reads.
static const OSStatus kKMSandboxDenied = 100001;

// The unified log redacts %@ arguments from this process, so rejections also emit
// their stage and OSStatus as integers, which are never redacted. Stage map:
//   0 no client, 1 bad pid,
//   3 SecRequirementCreateWithString, 4 SecCodeCheckValidity (mode A live),
//   5 signingID cross-check,
//   20 proc_pidpath (logged only - failure falls through),
//   21 SecStaticCodeCreateWithPath (mode B direct; EPERM falls to mode B'),
//   23 SecStaticCodeCheckValidity (mode B; genuine mismatch rejects),
//   24 plain read of the live path failed (mode B'), 25 staging write failed,
//   26 staged SecStaticCodeCreateWithPath, 27 staged SecStaticCodeCheckValidity,
//   32 evidence or live path unavailable, 33 recorded path != live pid path,
//   34 evidence SecStaticCodeCreateWithPath, 35 evidence SecStaticCodeCheckValidity,
//   49 no evidence found in any candidate directory (candidate order: every
//      local user's group container via passwd enumeration, own group container,
//      /private/tmp, /Users/Shared; misses with ENOENT/EACCES stay unlogged,
//      other errnos are logged with candidate index + errno, not rejected),
//   Mode K (key attestation, tried first): 50 no client / invalid pid,
//   60 challenge length invalid, 61 response missing or truncated,
//   62 timestamp outside window (status = delta seconds),
//   63 pinned public key unavailable, 64 ECDSA signature verification failed.
static BOOL KMRejectStatus(NSString* _Nullable* _Nullable reasonOut, NSString* reason,
                           int stage, int status, int pid) {
    NSLog(@"KMProducerAuth: reject stage=%d status=%d pid=%d", stage, status, pid);
    return KMReject(reasonOut, reason);
}

static SecRequirementRef _Nullable KMCreateRequirement(
    int pid, NSString* _Nullable* _Nullable reasonOut) {
    SecRequirementRef requirement = nullptr;
    OSStatus status = SecRequirementCreateWithString((__bridge CFStringRef)kKMHostProducerRequirement,
        kSecCSDefaultFlags, &requirement);
    if (status != errSecSuccess) {
        KMRejectStatus(reasonOut, [NSString stringWithFormat:
            @"SecRequirementCreateWithString -> %d", static_cast<int>(status)],
            3, static_cast<int>(status), pid);
        return nullptr;
    }
    return requirement;
}

// Cross-check the identity CMIO itself reports for this client, so a pid mix-up
// cannot pair the right code with the wrong CMIO connection. Stage 6 device runs
// showed CMIOExtensionClient.signingID == @"unknown" for properly signed
// processes (our host and Chrome alike), so an absent value is NOT treated as a
// mismatch: authorization rests on the OS-verified SecCode requirement, never on
// an unverified string alone (W6-2).
static BOOL KMCheckSigningID(CMIOExtensionClient* client, int pid,
                             NSString* _Nullable* _Nullable reasonOut) {
    if (@available(macOS 13.0, *)) {
        NSString* signingID = client.signingID;
        if (signingID.length > 0 && ![signingID isEqualToString:@"unknown"] &&
            ![signingID isEqualToString:kKMHostSigningIdentifier]) {
            return KMRejectStatus(reasonOut,
                [NSString stringWithFormat:@"signingID mismatch: %@", signingID],
                5, 0, pid);
        }
        NSLog(@"KMProducerAuth: signingID observed pid=%d signingID=%@",
              pid, signingID ?: @"(nil)");
    }
    return YES;
}

// Validates a static code reference against the producer requirement and the
// signingID cross-check; releases staticRef either way. On validity failure the
// raw OSStatus is returned through statusOut when provided, so callers can tell a
// sandbox denial (retryable via another mode) from a genuine signature mismatch.
static BOOL KMValidateStaticAndRelease(SecStaticCodeRef staticRef, int pid,
                                       CMIOExtensionClient* client,
                                       NSString* _Nullable* _Nullable reasonOut,
                                       int validityStage,
                                       OSStatus* _Nullable statusOut) {
    if (statusOut) *statusOut = errSecSuccess;
    SecRequirementRef requirement = KMCreateRequirement(pid, reasonOut);
    if (!requirement) { CFRelease(staticRef); return NO; }
    OSStatus status = SecStaticCodeCheckValidity(staticRef, kSecCSDefaultFlags, requirement);
    CFRelease(requirement);
    CFRelease(staticRef);
    if (status != errSecSuccess) {
        if (statusOut) *statusOut = status;
        return KMRejectStatus(reasonOut, [NSString stringWithFormat:
            @"static signature requirement not satisfied pid=%d -> %d",
            pid, static_cast<int>(status)], validityStage, static_cast<int>(status), pid);
    }
    if (!KMCheckSigningID(client, pid, reasonOut)) return NO;
    return YES;
}

// Mode K - in-band key attestation. The host reads this device's challenge
// property, signs (challenge || its own pid || unix seconds) with the producer
// private key held in its login keychain, writes the response property, and only
// then calls startStream - so the response is always present before this runs.
// Security basis (W6-2): only a process securityd lets use the private key can
// produce the signature; the key's ACL is the host's designated requirement,
// evaluated against its LIVE code signature at signing time (OS-verified). The
// pid inside the signed payload binds the attestation to THIS client, the
// timestamp is limited to kKMAuthMaxClockSkewSeconds, and the challenge is
// single-use (rotated when a response is accepted), so a captured response
// cannot be replayed - and none of it needs any file or keychain access from
// the sandboxed extension.
BOOL KMVerifyKeyAttestation(CMIOExtensionClient* client,
                            NSData* challenge,
                            NSData* response,
                            NSString* _Nullable* _Nullable reasonOut) {
    if (!client) return KMRejectStatus(reasonOut, @"no client", 50, 0, 0);
    const int pid = client.pid;
    if (pid <= 0)
        return KMRejectStatus(reasonOut,
            [NSString stringWithFormat:@"pid unavailable (%d)", pid], 50, 0, pid);
    if (challenge.length != kKMAuthChallengeLength)
        return KMRejectStatus(reasonOut, @"challenge length invalid", 60,
                              static_cast<int>(challenge.length), pid);
    if (response.length <= 8)
        return KMRejectStatus(reasonOut, @"attestation response missing", 61,
                              static_cast<int>(response.length), pid);

    const uint8_t* bytes = static_cast<const uint8_t*>(response.bytes);
    const int64_t signedTime = static_cast<int64_t>(KMReadLE64(bytes));
    const int64_t now = static_cast<int64_t>(time(nullptr));
    const int64_t delta = now > signedTime ? now - signedTime : signedTime - now;
    if (delta > kKMAuthMaxClockSkewSeconds)
        return KMRejectStatus(reasonOut, @"attestation timestamp outside window", 62,
                              static_cast<int>(delta), pid);

    static SecKeyRef publicKey = nullptr;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        NSData* encoded = [[NSData alloc]
            initWithBase64EncodedString:kKMHostProducerPubKeyBase64 options:0];
        if (encoded.length > 0) {
            NSDictionary* attributes = @{
                (__bridge NSString*)kSecAttrKeyType :
                    (__bridge NSString*)kSecAttrKeyTypeECSECPrimeRandom,
                (__bridge NSString*)kSecAttrKeyClass :
                    (__bridge NSString*)kSecAttrKeyClassPublic,
            };
            publicKey = SecKeyCreateWithData((__bridge CFDataRef)encoded,
                                              (__bridge CFDictionaryRef)attributes, nullptr);
        }
        NSLog(@"KMProducerAuth: pinned attestation public key status=%d encodedLen=%lu",
              publicKey ? 0 : 1, static_cast<unsigned long>(encoded.length));
    });
    if (!publicKey)
        return KMRejectStatus(reasonOut, @"attestation public key unavailable", 63, 0, pid);

    NSMutableData* payload = [NSMutableData dataWithData:challenge];
    uint8_t pidBytes[4];
    KMWriteLE32(pidBytes, static_cast<uint32_t>(pid));
    [payload appendBytes:pidBytes length:sizeof(pidBytes)];
    uint8_t timeBytes[8];
    KMWriteLE64(timeBytes, static_cast<uint64_t>(signedTime));
    [payload appendBytes:timeBytes length:sizeof(timeBytes)];
    NSData* signature = [response subdataWithRange:NSMakeRange(8, response.length - 8)];
    const Boolean verified = SecKeyVerifySignature(publicKey,
        kSecKeyAlgorithmECDSASignatureMessageX962SHA256, (__bridge CFDataRef)payload,
        (__bridge CFDataRef)signature, nullptr);
    if (!verified)
        return KMRejectStatus(reasonOut, @"attestation signature invalid", 64, 0, pid);
    if (!KMCheckSigningID(client, pid, reasonOut)) return NO;
    NSLog(@"KMProducerAuth: attestation verified pid=%d mode=key-attestation", pid);
    return YES;
}

BOOL KMVerifyHostProducer(CMIOExtensionClient* client,
                          NSString* _Nullable* _Nullable reasonOut) {
    if (!client) return KMRejectStatus(reasonOut, @"no client", 0, 0, 0);

    const int pid = client.pid;
    if (pid <= 0)
        return KMRejectStatus(reasonOut,
            [NSString stringWithFormat:@"pid unavailable (%d)", pid], 1, 0, pid);

    // Mode A - live code object. Binds the pid to kernel-validated dynamic code
    // (no TOCTOU window) and checks the designated requirement. The extension's
    // App Sandbox denies the cross-process lookup on this system (measured
    // status 100001), so a denial falls through to the static modes.
    NSDictionary* attributes = @{(__bridge NSString*)kSecGuestAttributePid : @(pid)};
    SecCodeRef code = nullptr;
    OSStatus status = SecCodeCopyGuestWithAttributes(nullptr,
        (__bridge CFDictionaryRef)attributes, kSecCSDefaultFlags, &code);
    if (status == errSecSuccess && code) {
        SecRequirementRef requirement = KMCreateRequirement(pid, reasonOut);
        if (!requirement) { CFRelease(code); return NO; }
        status = SecCodeCheckValidity(code, kSecCSDefaultFlags, requirement);
        CFRelease(requirement);
        CFRelease(code);
        if (status != errSecSuccess) {
            return KMRejectStatus(reasonOut, [NSString stringWithFormat:
                @"code requirement not satisfied pid=%d -> %d", pid, static_cast<int>(status)],
                4, static_cast<int>(status), pid);
        }
        if (!KMCheckSigningID(client, pid, reasonOut)) return NO;
        NSLog(@"KMProducerAuth: authorized pid=%d mode=live-sec-code", pid);
        return YES;
    }
    NSLog(@"KMProducerAuth: live lookup unavailable pid=%d status=%d, trying static modes",
          pid, static_cast<int>(status));

    // Live path of the connecting pid - kernel-reported via proc_pidpath (this
    // call works from the sandbox: measured stage-20 success) - is the binding
    // used by both static modes below.
    // proc_pidpath's documented buffer bound is 4096 bytes.
    char pathBuf[4096] = {};
    ssize_t pathLen = proc_pidpath(pid, pathBuf, sizeof(pathBuf));
    NSString* livePath = pathLen > 0 ? [NSString stringWithUTF8String:pathBuf] : nil;
    if (!livePath)
        NSLog(@"KMProducerAuth: proc_pidpath failed pid=%d status=%d", pid, errno);

    // Mode B - static validation of the live path itself. Works when the caller
    // may read that file; in the current sandbox the open is denied (EPERM) and
    // we fall through to mode C.
    if (livePath) {
        SecStaticCodeRef staticCode = nullptr;
        status = SecStaticCodeCreateWithPath((__bridge CFURLRef)[NSURL fileURLWithPath:livePath],
            kSecCSDefaultFlags, &staticCode);
        if (status == errSecSuccess && staticCode) {
            OSStatus validityStatus = errSecSuccess;
            BOOL ok = KMValidateStaticAndRelease(staticCode, pid, client, reasonOut,
                                                 23, &validityStatus);
            if (ok) {
                NSLog(@"KMProducerAuth: authorized pid=%d mode=path-static", pid);
                return YES;
            }
            if (validityStatus != kKMSandboxDenied) return NO;  // genuine mismatch/reject
            if (reasonOut) *reasonOut = nil;  // sandbox denial: fall through to mode C
        } else {
            NSLog(@"KMProducerAuth: direct static open failed pid=%d status=%d%s",
                  pid, static_cast<int>(status),
                  status == kKMSandboxDenied ? " (sandbox denied)" : "");
        }
    }

    // Mode B' - staged copy of the live executable. When Security cannot open
    // the live path directly (measured EPERM from SecStaticCodeCreateWithPath),
    // read the file bytes plainly from proc_pidpath's kernel-reported path,
    // stage them in THIS process's own group container - the one location the
    // sandbox both lets this process write and read - and validate the staged
    // copy. The path binding is inherent (bytes are read from the live path
    // itself); the residual race (file replaced between path lookup and read)
    // is the same exec-swap class already documented for modes B/C, and the
    // authorization decision still rests solely on the OS-verified signature
    // check below - never on bytes/pid/strings alone.
    if (livePath) {
        NSError* readError = nil;
        NSData* liveBytes = [NSData dataWithContentsOfFile:livePath options:0 error:&readError];
        if (liveBytes.length > 0) {
            NSFileManager* fm = [NSFileManager defaultManager];
            NSURL* container = [fm containerURLForSecurityApplicationGroupIdentifier:
                kKMAppGroupIdentifier];
            if (container) [fm createDirectoryAtURL:container withIntermediateDirectories:YES
                                         attributes:nil error:nil];
            NSURL* stagedURL = [container URLByAppendingPathComponent:
                [NSString stringWithFormat:@"producer-evidence-%d.bin", pid]];
            NSError* writeError = nil;
            const BOOL wrote = container &&
                [liveBytes writeToURL:stagedURL options:NSDataWritingAtomic error:&writeError];
            NSLog(@"KMProducerAuth: live plain read bytes=%lu stage write status=%d",
                  static_cast<unsigned long>(liveBytes.length),
                  wrote ? 0 : static_cast<int>(writeError.code));
            if (wrote) {
                SecStaticCodeRef staged = nullptr;
                status = SecStaticCodeCreateWithPath((__bridge CFURLRef)stagedURL,
                    kSecCSDefaultFlags, &staged);
                if (status == errSecSuccess && staged) {
                    OSStatus validityStatus = errSecSuccess;
                    BOOL ok = KMValidateStaticAndRelease(staged, pid, client, reasonOut,
                                                         27, &validityStatus);
                    [fm removeItemAtURL:stagedURL error:nil];
                    if (ok) {
                        NSLog(@"KMProducerAuth: authorized pid=%d mode=staged-live-copy", pid);
                        return YES;
                    }
                    if (validityStatus != kKMSandboxDenied) return NO;
                    if (reasonOut) *reasonOut = nil;  // sandbox denial: continue to mode C
                } else {
                    [fm removeItemAtURL:stagedURL error:nil];
                    NSLog(@"KMProducerAuth: staged static create status=%d",
                          static_cast<int>(status));
                }
            }
        } else {
            NSLog(@"KMProducerAuth: live plain read failed pid=%d status=%d",
                  pid, readError ? static_cast<int>(readError.code) : errno);
        }
    }

    // Mode C - shared-container evidence. The host publishes a byte copy of its
    // own executable plus its run path wherever both sides may write (group
    // container first, then neutral paths - a system extension resolves its group
    // container at a different path than this process, measured ENOENT). Writing
    // requires no privilege on /private/tmp-style paths, so bytes alone are not
    // trusted: authorization requires BOTH
    //   1. the recorded path equals this pid's live proc_pidpath result
    //      (kernel path binding - an exec-swap changes the live path), and
    //   2. Security validates the copy against the same requirement
    //      (OS-verified signature - never pid / name / unsigned string alone).
    NSMutableArray<NSURL*>* candidates = [NSMutableArray array];

    // 1. Every local user's group container. App-group containers resolve
    //    per-user (the host's uid 502 home and this extension's uid 262 home
    //    yield different paths), and the sandbox denies learning the client's
    //    uid across the process boundary (proc_pidinfo measured EPERM), so
    //    enumerate the passwd database and probe each home's group container
    //    for the evidence the host published. The discovered path is only a
    //    LOCATOR: trust still comes from the live proc_pidpath binding plus
    //    SecStaticCodeCheckValidity against kKMHostProducerRequirement below,
    //    so a wrong or hijacked location cannot authorize anyone (forging
    //    evidence requires our team's signing key).
    NSString* groupRelDir = [@"Library/Group Containers"
        stringByAppendingPathComponent:kKMAppGroupIdentifier];
    NSUInteger probeCount = 0;
    setpwent();
    while (struct passwd* pw = getpwent()) {
        if (!pw->pw_dir || pw->pw_dir[0] != '/') continue;
        NSString* home = [NSString stringWithUTF8String:pw->pw_dir];
        if (home.length == 0) continue;
        NSURL* url = [NSURL fileURLWithPath:[home stringByAppendingPathComponent:groupRelDir]];
        if ([candidates containsObject:url]) continue;
        [candidates addObject:url];
        if (++probeCount >= 128) break;  // bound the probe loop
    }
    endpwent();
    NSLog(@"KMProducerAuth: passwd probe candidates=%lu",
          static_cast<unsigned long>(probeCount));

    // 2. This process's own group container (where the host would land if both
    // ever resolve identically).
    NSURL* container = [[NSFileManager defaultManager]
        containerURLForSecurityApplicationGroupIdentifier:kKMAppGroupIdentifier];
    if (container) {
        [candidates addObject:container];
        // '/' would be content-redacted as <private> in the unified log, so
        // sanitize separators to keep the real path visible for diagnosis.
        NSString* shown = [container.path stringByReplacingOccurrencesOfString:@"/"
                                                                     withString:@"#"];
        NSLog(@"KMProducerAuth: own container path=%s",
              shown.fileSystemRepresentation);
    } else {
        NSLog(@"KMProducerAuth: group container unavailable status=0");
    }

    // 3-4. Neutral paths (host sandbox measured to deny both - kept as probes).
    for (NSString* dir in @[ @"/private/tmp/K7VNGA9K78.com.mat2uken.kmvirtualcamera",
                            @"/Users/Shared/K7VNGA9K78.com.mat2uken.kmvirtualcamera" ])
        [candidates addObject:[NSURL fileURLWithPath:dir]];

    NSURL* evidenceDir = nil;
    NSData* recordedData = nil;
    for (NSUInteger i = 0; i < candidates.count; i++) {
        NSURL* recordedURL = [candidates[i] URLByAppendingPathComponent:kKMHostEvidencePathName];
        recordedData = [NSData dataWithContentsOfURL:recordedURL options:0 error:nil];
        if (recordedData.length > 0) { evidenceDir = candidates[i]; break; }
        // Measured miss map: ENOENT (no such evidence) and EACCES (POSIX denies
        // uid 262 the traverse to other users' homes) are the expected outcomes,
        // so only unexpected errnos are logged per candidate (every 2s retry).
        if (errno != ENOENT && errno != EACCES)
            NSLog(@"KMProducerAuth: evidence candidate %lu missing status=%d",
                  static_cast<unsigned long>(i), errno);
    }
    if (!evidenceDir || recordedData.length == 0)
        return KMRejectStatus(reasonOut, @"host executable evidence missing",
                              49, errno, pid);
    NSString* recordedPath =
        [[NSString alloc] initWithData:recordedData encoding:NSUTF8StringEncoding];
    if (recordedPath.length == 0 || !livePath)
        return KMRejectStatus(reasonOut, @"evidence or live path unavailable",
                              32, 0, pid);
    if (![recordedPath isEqualToString:livePath]) {
        NSLog(@"KMProducerAuth: evidence path mismatch recordedLen=%lu liveLen=%lu",
              static_cast<unsigned long>(recordedPath.length),
              static_cast<unsigned long>(livePath.length));
        return KMRejectStatus(reasonOut, @"evidence path does not match client pid path",
                              33, 0, pid);
    }

    NSURL* binaryURL = [evidenceDir URLByAppendingPathComponent:kKMHostEvidenceBinaryName];
    SecStaticCodeRef evidenceCode = nullptr;
    status = SecStaticCodeCreateWithPath((__bridge CFURLRef)binaryURL,
        kSecCSDefaultFlags, &evidenceCode);
    if (status != errSecSuccess || !evidenceCode) {
        return KMRejectStatus(reasonOut, [NSString stringWithFormat:
            @"evidence SecStaticCodeCreateWithPath -> %d", static_cast<int>(status)],
            34, static_cast<int>(status), pid);
    }
    if (!KMValidateStaticAndRelease(evidenceCode, pid, client, reasonOut, 35, nullptr))
        return NO;
    NSLog(@"KMProducerAuth: authorized pid=%d mode=container-evidence", pid);
    return YES;
}
