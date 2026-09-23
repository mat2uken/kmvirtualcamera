#include "../receiver/native_media.h"
#include "km/h264.h"
#import <Foundation/Foundation.h>
#include <fstream>
#include <iostream>
#include <vector>
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
        if (argc != 3 || std::string(argv[1]) != "--decode-au") {
            std::cerr << "Usage: km_macos_media_smoke [--decode-au one-annexb-access-unit.h264]\n";
            return 2;
        }
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
        return 0;
    }
}
