#include "../third_party/qr/qrcodegen.hpp"
#include <iostream>
#include <fstream>
#include <string>
#include <vector>

#pragma pack(push, 1)
struct BMPHeader {
    uint16_t bfType{0x4D42}; // 'BM'
    uint32_t bfSize{0};
    uint16_t bfReserved1{0};
    uint16_t bfReserved2{0};
    uint32_t bfOffBits{54};
    uint32_t biSize{40};
    int32_t  biWidth{0};
    int32_t  biHeight{0};
    uint16_t biPlanes{1};
    uint16_t biBitCount{24};
    uint32_t biCompression{0};
    uint32_t biSizeImage{0};
    int32_t  biXPelsPerMeter{2835};
    int32_t  biYPelsPerMeter{2835};
    uint32_t biClrUsed{0};
    uint32_t biClrImportant{0};
};
#pragma pack(pop)

void SaveQrToBmp(const qrcodegen::QrCode& qr, const std::string& filename, int scale = 6, int border = 4) {
    int qrSize = qr.getSize();
    int totalModules = qrSize + border * 2;
    int imgSize = totalModules * scale;
    int rowSize = (imgSize * 3 + 3) & ~3; // 4-byte aligned
    int dataSize = rowSize * imgSize;

    BMPHeader header;
    header.bfSize = 54 + dataSize;
    header.biWidth = imgSize;
    header.biHeight = imgSize; // bottom-up
    header.biSizeImage = dataSize;

    std::vector<uint8_t> buffer(dataSize, 255); // White background

    for (int y = 0; y < imgSize; ++y) {
        int qrY = (imgSize - 1 - y) / scale - border;
        uint8_t* row = buffer.data() + y * rowSize;
        for (int x = 0; x < imgSize; ++x) {
            int qrX = x / scale - border;
            bool isBlack = false;
            if (qrX >= 0 && qrX < qrSize && qrY >= 0 && qrY < qrSize) {
                isBlack = qr.getModule(qrX, qrY);
            }
            if (isBlack) {
                row[x * 3 + 0] = 0;
                row[x * 3 + 1] = 0;
                row[x * 3 + 2] = 0;
            }
        }
    }

    std::ofstream out(filename, std::ios::binary);
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    out.write(reinterpret_cast<const char*>(buffer.data()), buffer.size());
}

int main() {
    std::string text = "https://webrtc-bridge-signaling.mat2uken.workers.dev/send/#v=1&s=49np4uJjdIrs7bQSGEN09g&j=2SQVOAxgc_wrpMlG6UTdZZJ8M0sbtDWQ4Ncp27nTXaQ";
    std::cout << "Testing QR Generation with official Nayuki library for URL:" << std::endl;
    std::cout << "  " << text << std::endl;

    qrcodegen::QrCode qr = qrcodegen::QrCode::encodeText(text.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
    std::cout << "Generated QR Code Version: " << qr.getVersion() << ", Size: " << qr.getSize() << "x" << qr.getSize() << std::endl;

    SaveQrToBmp(qr, "test_generated_qr.bmp", 8, 4);
    std::cout << "[SUCCESS] Saved QR code image to test_generated_qr.bmp!" << std::endl;
    return 0;
}
