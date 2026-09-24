#import "host_exec_publisher.h"
#import "../camera-extension/ids.h"

// Neutral evidence directories, probed by the extension in the same order
// (producer_auth mode C). The group container stays first in both lists; these
// two exist because a system extension resolves its group container at a
// different path than this app (measured: extension stage-31 ENOENT on the
// user's container, and /Library/Group Containers does not exist). The team-ID
// directory name avoids collisions; /private/tmp is sticky world-writable, so a
// pre-created foreign directory can block publication (availability only - the
// extension's path binding and OS signature check still apply). Dev values.
static NSArray<NSString*>* KMEvidenceDirectoryPaths(void) {
    NSString* leaf = @"K7VNGA9K78.com.mat2uken.kmvirtualcamera";
    return @[
        [@"/private/tmp" stringByAppendingPathComponent:leaf],
        [@"/Users/Shared" stringByAppendingPathComponent:leaf],
    ];
}

static void KMPublishInto(NSURL* directory, NSData* executable, NSData* pathData,
                          NSString* label) {
    NSFileManager* fm = [NSFileManager defaultManager];
    NSError* error = nil;
    if (![fm createDirectoryAtURL:directory withIntermediateDirectories:YES
                       attributes:@{NSFilePosixPermissions : @(0755)} error:&error]) {
        NSLog(@"KMEvidence: %@ create dir failed: %@", label, error);
        return;
    }
    NSURL* binaryURL = [directory URLByAppendingPathComponent:kKMHostEvidenceBinaryName];
    if (![executable writeToURL:binaryURL options:NSDataWritingAtomic error:&error]) {
        NSLog(@"KMEvidence: %@ write binary failed: %@", label, error);
        return;
    }
    NSURL* pathURL = [directory URLByAppendingPathComponent:kKMHostEvidencePathName];
    if (![pathData writeToURL:pathURL options:NSDataWritingAtomic error:&error]) {
        NSLog(@"KMEvidence: %@ write path failed: %@", label, error);
        return;
    }
    // The extension runs as another user (_cmiodalassistants): keep the files
    // world-readable and directories traversable.
    [fm setAttributes:@{NSFilePosixPermissions : @(0644)} ofItemAtPath:binaryURL.path error:nil];
    [fm setAttributes:@{NSFilePosixPermissions : @(0644)} ofItemAtPath:pathURL.path error:nil];
    [fm setAttributes:@{NSFilePosixPermissions : @(0755)} ofItemAtPath:directory.path error:nil];
    NSLog(@"KMEvidence: %@ published bytes=%lu", label,
          static_cast<unsigned long>(executable.length));
}

// Writes host-executable.bin + host-executable.path wherever this process may
// write; the extension tries the same candidate list. Called at launch and on
// every sink-start so the recorded path always matches the running instance.
void KMHostPublishExecutableEvidence(void) {
    NSString* exePath = [NSBundle mainBundle].executablePath;
    if (exePath.length == 0) {
        NSLog(@"KMEvidence: executablePath unavailable");
        return;
    }
    NSError* error = nil;
    NSData* executable = [NSData dataWithContentsOfFile:exePath options:0 error:&error];
    if (!executable) {
        NSLog(@"KMEvidence: read own executable failed: %@", error);
        return;
    }
    NSData* pathData = [exePath dataUsingEncoding:NSUTF8StringEncoding];

    // 1. Canonical App Group container.
    NSURL* container = [[NSFileManager defaultManager]
        containerURLForSecurityApplicationGroupIdentifier:kKMAppGroupIdentifier];
    if (container) {
        KMPublishInto(container, executable, pathData, @"group");
    } else {
        NSLog(@"KMEvidence: group container unavailable group=%@", kKMAppGroupIdentifier);
    }

    // 2+. Neutral paths (see KMEvidenceDirectoryPaths).
    for (NSString* path in KMEvidenceDirectoryPaths())
        KMPublishInto([NSURL fileURLWithPath:path], executable, pathData,
                      [NSString stringWithFormat:@"ext:%@", path.lastPathComponent]);
}
