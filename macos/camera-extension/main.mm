#import <CoreMediaIO/CMIOExtension.h>
#import "provider.h"

// Stage 5 (W5-1..W5-3): start the service, register the single device, run the
// run loop. Provider/device/stream sources live in provider.mm, device.mm and
// source_stream.mm; everything runs on one serial queue so the stage 6 relay
// can join the same queue later.
int main(int argc, const char* argv[]) {
    (void)argc;
    (void)argv;
    @autoreleasepool {
        dispatch_queue_t queue = dispatch_queue_create(
            "jp.km.virtualcamera.provider", DISPATCH_QUEUE_SERIAL);
        KMExtensionProviderSource* source =
            [[KMExtensionProviderSource alloc] initWithClientQueue:queue];
        [CMIOExtensionProvider startServiceWithProvider:source.provider];
        [source installDevice];
        CFRunLoopRun();
    }
    return 0;
}
