#import "host_auth_signer.h"
#import "../camera-extension/ids.h"
#import <Security/Security.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

namespace {
// Application tag identifying the producer auth key in the login keychain.
NSData* KMAuthKeyTag(void) {
    static const char kTag[] = "jp.km.virtualcamera.producer-auth";
    return [NSData dataWithBytes:kTag length:sizeof(kTag) - 1];
}

// Looks up the producer auth private key. Returns an owned reference or nullptr
// with *statusOut set to the SecItemCopyMatching status (errSecItemNotFound
// when the key has not been provisioned yet).
SecKeyRef KMFindAuthKey(OSStatus* statusOut) {
    NSDictionary* query = @{
        (__bridge NSString*)kSecClass : (__bridge NSString*)kSecClassKey,
        (__bridge NSString*)kSecAttrApplicationTag : KMAuthKeyTag(),
        (__bridge NSString*)kSecAttrKeyClass : (__bridge NSString*)kSecAttrKeyClassPrivate,
        (__bridge NSString*)kSecMatchLimit : (__bridge NSString*)kSecMatchLimitOne,
        (__bridge NSString*)kSecReturnRef : @YES,
    };
    SecKeyRef key = nullptr;
    const OSStatus status =
        SecItemCopyMatching((__bridge CFDictionaryRef)query, (CFTypeRef*)&key);
    if (statusOut) *statusOut = status;
    if (status != errSecSuccess) return nullptr;
    return key;
}
}  // namespace

NSData* KMHostProducerAuthPublicKey(BOOL rotate, int* statusOut) {
    if (rotate) {
        NSDictionary* deleteQuery = @{
            (__bridge NSString*)kSecClass : (__bridge NSString*)kSecClassKey,
            (__bridge NSString*)kSecAttrApplicationTag : KMAuthKeyTag(),
        };
        const OSStatus deleteStatus =
            SecItemDelete((__bridge CFDictionaryRef)deleteQuery);
        NSLog(@"KMAuth: rotation delete status=%d", static_cast<int>(deleteStatus));
    }
    OSStatus status = errSecSuccess;
    SecKeyRef key = KMFindAuthKey(&status);
    if (!key) {
        if (status != errSecItemNotFound) {
            if (statusOut) *statusOut = static_cast<int>(status);
            NSLog(@"KMAuth: key lookup failed status=%d", static_cast<int>(status));
            return nil;
        }
        NSDictionary* attributes = @{
            (__bridge NSString*)kSecAttrKeyType :
                (__bridge NSString*)kSecAttrKeyTypeECSECPrimeRandom,
            (__bridge NSString*)kSecAttrKeySizeInBits : @256,
            (__bridge NSString*)kSecAttrIsPermanent : @YES,
            (__bridge NSString*)kSecAttrApplicationTag : KMAuthKeyTag(),
            (__bridge NSString*)kSecAttrLabel : @"KMVirtualCamera producer auth (dev)",
        };
        CFErrorRef error = nullptr;
        key = SecKeyCreateRandomKey((__bridge CFDictionaryRef)attributes, &error);
        if (!key) {
            const int code = error ? static_cast<int>(CFErrorGetCode(error)) : -1;
            if (error) CFRelease(error);
            if (statusOut) *statusOut = code;
            NSLog(@"KMAuth: key creation failed status=%d", code);
            return nil;
        }
        NSLog(@"KMAuth: producer auth key created");
    } else {
        NSLog(@"KMAuth: producer auth key found, re-exporting public key");
    }
    SecKeyRef publicKey = SecKeyCopyPublicKey(key);
    CFRelease(key);
    if (!publicKey) {
        if (statusOut) *statusOut = -1;
        NSLog(@"KMAuth: SecKeyCopyPublicKey failed status=0");
        return nil;
    }
    CFErrorRef error = nullptr;
    CFDataRef external = SecKeyCopyExternalRepresentation(publicKey, &error);
    CFRelease(publicKey);
    if (!external) {
        const int code = error ? static_cast<int>(CFErrorGetCode(error)) : -1;
        if (error) CFRelease(error);
        if (statusOut) *statusOut = code;
        NSLog(@"KMAuth: public key export failed status=%d", code);
        return nil;
    }
    NSData* result = [(__bridge NSData*)external copy];
    CFRelease(external);
    if (statusOut) *statusOut = 0;
    return result;
}

NSData* KMHostSignProducerAuth(NSData* challenge, int* statusOut) {
    if (challenge.length != kKMAuthChallengeLength) {
        if (statusOut) *statusOut = 1;
        NSLog(@"KMAuth: sign rejected: challenge length=%lu",
              static_cast<unsigned long>(challenge.length));
        return nil;
    }
    OSStatus status = errSecSuccess;
    SecKeyRef key = KMFindAuthKey(&status);
    if (!key) {
        if (statusOut) *statusOut = static_cast<int>(status);
        NSLog(@"KMAuth: producer key unavailable status=%d "
              @"(run scripts/provision_macos_auth_key.sh)",
              static_cast<int>(status));
        return nil;
    }
    const uint64_t now = static_cast<uint64_t>(time(nullptr));
    NSMutableData* payload = [NSMutableData dataWithData:challenge];
    uint8_t pidBytes[4];
    KMWriteLE32(pidBytes, static_cast<uint32_t>(getpid()));
    [payload appendBytes:pidBytes length:sizeof(pidBytes)];
    uint8_t timeBytes[8];
    KMWriteLE64(timeBytes, now);
    [payload appendBytes:timeBytes length:sizeof(timeBytes)];

    CFErrorRef error = nullptr;
    CFDataRef signature = SecKeyCreateSignature(key,
        kSecKeyAlgorithmECDSASignatureMessageX962SHA256, (__bridge CFDataRef)payload, &error);
    CFRelease(key);
    if (!signature) {
        const int code = error ? static_cast<int>(CFErrorGetCode(error)) : -1;
        if (error) CFRelease(error);
        if (statusOut) *statusOut = code;
        NSLog(@"KMAuth: signature failed status=%d", code);
        return nil;
    }
    NSMutableData* response = [NSMutableData dataWithBytes:timeBytes length:sizeof(timeBytes)];
    [response appendData:(__bridge NSData*)signature];
    CFRelease(signature);
    if (statusOut) *statusOut = 0;
    return response;
}
