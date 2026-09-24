#include "../receiver/native_media.h"
#include "km/h264.h"
#import <Foundation/Foundation.h>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {
std::string Describe(CFTypeRef value) {
    if (!value) return "(absent)";
    CFStringRef desc = CFCopyDescription(value);
    if (!desc) return "(unknown)";
    char buffer[512];
    const CFIndex used = CFStringGetCString(desc, buffer, sizeof(buffer), kCFStringEncodingUTF8);
    CFRelease(desc);
    return used ? std::string(buffer) : "(unconvertible)";
}

void PrintInspection(const char* label, CVPixelBufferRef pb) {
    std::cout << label
              << " format=0x" << std::hex << CVPixelBufferGetPixelFormatType(pb) << std::dec
              << " " << CVPixelBufferGetWidth(pb) << "x" << CVPixelBufferGetHeight(pb)
              << " planes=" << CVPixelBufferGetPlaneCount(pb)
              << " strideY=" << CVPixelBufferGetBytesPerRowOfPlane(pb, 0)
              << " strideUV=" << CVPixelBufferGetBytesPerRowOfPlane(pb, 1) << "\n";
    for (CFStringRef key : {kCVImageBufferCleanApertureKey, kCVImageBufferPixelAspectRatioKey,
                            kCVImageBufferYCbCrMatrixKey, kCVImageBufferColorPrimariesKey,
                            kCVImageBufferTransferFunctionKey}) {
        // CVBufferGetAttachment is deprecated since macOS 12.0; copy matches its
        // behavior for the snapshot we print and must be released.
        CFTypeRef attachment = CVBufferCopyAttachment(pb, key, nullptr);
        std::cout << "  " << CFStringGetCStringPtr(key, kCFStringEncodingUTF8) << "="
                  << Describe(attachment) << "\n";
        if (attachment) CFRelease(attachment);
    }
}

// Tightly packed NV12 dump (width rows per plane) for image comparison via ffmpeg.
bool DumpNv12(CVPixelBufferRef pb, const std::string& path, std::string& error) {
    if (CVPixelBufferGetPixelFormatType(pb) != kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange ||
        CVPixelBufferGetPlaneCount(pb) != 2) {
        error = "Dump requires 420v bi-planar input";
        return false;
    }
    if (CVPixelBufferLockBaseAddress(pb, kCVPixelBufferLock_ReadOnly) != kCVReturnSuccess) {
        error = "Cannot lock pixel buffer";
        return false;
    }
    bool ok = true;
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) { error = "Cannot open dump path"; ok = false; }
        else {
            const int width = CVPixelBufferGetWidth(pb), height = CVPixelBufferGetHeight(pb);
            for (int plane = 0; plane < 2 && ok; ++plane) {
                const int rows = plane ? height / 2 : height;
                const auto* row = static_cast<const uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(pb, plane));
                const size_t stride = CVPixelBufferGetBytesPerRowOfPlane(pb, plane);
                for (int y = 0; y < rows && ok; ++y)
                    out.write(reinterpret_cast<const char*>(row + y * stride), width),
                    ok = static_cast<bool>(out);
            }
        }
    }
    CVPixelBufferUnlockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);
    return ok;
}

int Usage() {
    std::cerr << "Usage: km_macos_media_smoke [--decode-au one-annexb-access-unit.h264"
              << " [--dump-normalized out.nv12]]\n";
    return 2;
}
} // namespace

int main(int argc, char** argv) {
    @autoreleasepool {
        std::string error;
        if (argc == 1) {
            auto pattern = km::mac::MakeBlack720p(error);
            if (!pattern) { std::cerr << error << "\n"; return 1; }
            auto normalized = km::mac::Normalize720p(pattern.get(), 0, error);
            if (!normalized) { std::cerr << error << "\n"; return 1; }
            std::cout << "720p 420v allocation/normalization OK (NOT camera registration)\n";
            return 0;
        }
        if (argc < 3 || std::string(argv[1]) != "--decode-au") return Usage();
        std::string dumpPath;
        if (argc == 5 && std::string(argv[3]) == "--dump-normalized") dumpPath = argv[4];
        else if (argc != 3) return Usage();
        std::ifstream input(argv[2], std::ios::binary | std::ios::ate);
        if (!input) { std::cerr << "Cannot open input\n"; return 2; }
        const auto size = input.tellg();
        if (size <= 0 || size > std::streamoff(km::h264::kMaxAccessUnitBytes)) {
            std::cerr << "Input must be one AU, 1..4MiB\n"; return 2;
        }
        input.seekg(0);
        std::vector<uint8_t> bytes(static_cast<size_t>(size));
        if (!input.read(reinterpret_cast<char*>(bytes.data()), size)) return 2;
        km::mac::VideoToolboxDecoder decoder;
        auto image = decoder.decode(bytes, 0, error);
        if (!image) { std::cerr << "No decoded image: " << error << "\n"; return 1; }
        std::cout << CVPixelBufferGetWidth(image.get()) << "x" << CVPixelBufferGetHeight(image.get())
                  << " hardware=" << decoder.hardwareActive() << "\n";
        PrintInspection("decoded", image.get());
        int failures = 0;
        for (int rotation : {0, 90, 180, 270}) {
            auto normalized = km::mac::Normalize720p(image.get(), rotation, error);
            if (!normalized) {
                std::cout << "normalize rotation=" << rotation << " FAILED: " << error << "\n";
                ++failures;
                continue;
            }
            const bool identity = normalized.get() == image.get();
            std::cout << "normalize rotation=" << rotation
                      << (identity ? " identity" : " transformed")
                      << " out=" << CVPixelBufferGetWidth(normalized.get()) << "x"
                      << CVPixelBufferGetHeight(normalized.get()) << "\n";
            if (rotation == 0) PrintInspection("normalized", normalized.get());
            if (rotation == 0 && !dumpPath.empty() && !DumpNv12(normalized.get(), dumpPath, error)) {
                std::cerr << "Dump failed: " << error << "\n";
                ++failures;
            }
        }
        return failures ? 1 : 0;
    }
}
