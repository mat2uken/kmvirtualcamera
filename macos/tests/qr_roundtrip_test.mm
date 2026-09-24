#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>
#import <Vision/Vision.h>
#include "../../windows/third_party/qr/qrcodegen.hpp"
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ":" << __LINE__ << ": " #x "\n"; return 1; } } while (false)

// Join-QR round trip: generate with the very generator the Windows UI uses
// (qrcodegen, Ecc::MEDIUM, quiet zone 4 - windows/receiver/ui/qr_view.cpp),
// render to a grayscale bitmap, and decode with Apple's Vision barcode reader
// (an independent decoder, i.e. what a phone camera does). This is the
// plan step 5 "入出力形式の照合" evidence that runs without a scanner.
namespace {

constexpr int kBorder = 4;

// Renders the QR into a grayscale bitmap and returns the CGImage (caller releases).
// Module (0,0) is the top-left; CoreGraphics' bottom-left origin is compensated.
CGImageRef RenderQr(const qrcodegen::QrCode& qr, int cell) {
    const int size = qr.getSize();
    const int total = size + kBorder * 2;
    const int px = cell * total;
    std::vector<uint8_t> pixels(size_t(px) * size_t(px), 0xFF); // white quiet zone
    CGColorSpaceRef gray = CGColorSpaceCreateDeviceGray();
    CGContextRef ctx = CGBitmapContextCreate(pixels.data(), px, px, 8, px, gray, kCGImageAlphaNone);
    CGColorSpaceRelease(gray);
    if (!ctx) return nullptr;
    CGContextSetGrayFillColor(ctx, 0.0, 1.0);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            if (!qr.getModule(x, y)) continue;
            const CGFloat left = CGFloat((kBorder + x) * cell);
            const CGFloat bottom = CGFloat((total - kBorder - y - 1) * cell);
            CGContextFillRect(ctx, CGRectMake(left, bottom, CGFloat(cell), CGFloat(cell)));
        }
    }
    CGImageRef image = CGBitmapContextCreateImage(ctx);
    CGContextRelease(ctx);
    return image;
}

bool decodeMatches(CGImageRef image, const std::string& expected) {
    @autoreleasepool {
        NSError* error = nil;
        VNDetectBarcodesRequest* request = [[VNDetectBarcodesRequest alloc] init];
        request.symbologies = @[ VNBarcodeSymbologyQR ];
        VNImageRequestHandler* handler =
            [[VNImageRequestHandler alloc] initWithCGImage:image options:@{}];
        if (![handler performRequests:@[ request ] error:&error]) {
            std::cerr << "Vision failed: " << (error ? error.localizedDescription.UTF8String : "?") << "\n";
            return false;
        }
        for (VNBarcodeObservation* observation in request.results) {
            NSString* payload = observation.payloadStringValue;
            if (payload && [payload isEqualToString:[NSString stringWithUTF8String:expected.c_str()]]) return true;
        }
        return false;
    }
}

} // namespace

int main() {
    @autoreleasepool {
        const std::vector<std::string> payloads = {
            // Short join URL, URL with query, and a Cloudflare-length join URL.
            "https://example.test/send/sess-42",
            "https://example.test/send/sess-42?token=T0k3n-abc_123&x=1",
            "https://kmvirtualcamera.example.workers.dev/send/"
            "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
        };
        for (const auto& payload : payloads) {
            const qrcodegen::QrCode qr = qrcodegen::QrCode::encodeText(payload.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
            CHECK(qr.getSize() >= 21); // QR version 1 minimum
            // Deterministic: the same payload must produce the same matrix.
            const qrcodegen::QrCode again = qrcodegen::QrCode::encodeText(payload.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
            CHECK(again.getSize() == qr.getSize());
            for (int y = 0; y < qr.getSize(); ++y)
                for (int x = 0; x < qr.getSize(); ++x) CHECK(again.getModule(x, y) == qr.getModule(x, y));
            // Cell size mirrors the Windows view: min(width,height) / (size+2*border).
            const int cell = std::max(1, 160 / (qr.getSize() + kBorder * 2));
            CGImageRef image = RenderQr(qr, cell);
            CHECK(image != nullptr);
            const bool decoded = decodeMatches(image, payload);
            CGImageRelease(image);
            if (!decoded) {
                std::cerr << __FILE__ << ":" << __LINE__ << ": QR round trip failed for payload of size "
                          << payload.size() << "\n";
                return 1;
            }
        }
    }
    std::cout << "macos_qr_roundtrip: qrcodegen(MEDIUM, border 4) -> bitmap -> Vision QR decode matched\n";
    return 0;
}
