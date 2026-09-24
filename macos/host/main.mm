#import <AppKit/AppKit.h>
#import "app_delegate.h"
#import "host_auth_signer.h"
#include <stdio.h>
#include <string.h>

int main(int argc, const char* argv[]) {
    @autoreleasepool {
        // W6-2 mode K provisioning: create (or re-export) the producer auth key
        // in this app's login keychain and print the PUBLIC key as one base64
        // line on stdout for scripts/provision_macos_auth_key.sh. Runs before
        // NSApplication so it never opens a window. The private key stays in the
        // keychain; nothing secret is printed or written here.
        if (argc >= 2 && strcmp(argv[1], "--provision-auth-key") == 0) {
            const BOOL rotate = argc >= 3 && strcmp(argv[2], "--rotate") == 0;
            int status = 0;
            NSData* publicKey = KMHostProducerAuthPublicKey(rotate, &status);
            if (!publicKey) {
                fprintf(stderr, "provision failed status=%d\n", status);
                return 1;
            }
            NSString* base64 = [publicKey base64EncodedStringWithOptions:0];
            printf("%s\n", base64.UTF8String);
            return 0;
        }
        NSApplication* app = [NSApplication sharedApplication];
        KMAppDelegate* delegate = [[KMAppDelegate alloc] init];
        app.delegate = delegate;
        [app run];
    }
    return 0;
}
