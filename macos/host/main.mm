#import <AppKit/AppKit.h>
#import "app_delegate.h"

int main(int argc, const char* argv[]) {
    @autoreleasepool {
        NSApplication* app = [NSApplication sharedApplication];
        KMAppDelegate* delegate = [[KMAppDelegate alloc] init];
        app.delegate = delegate;
        [app run];
    }
    return 0;
}
