#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <windows.h>
#include "../receiver/codec/h264_decoder.h"

#pragma pack(push, 1)
struct BmpFileHeader {
    uint16_t bfType{ 0x4D42 };
    uint32_t bfSize{ 0 };
    uint16_t bfReserved1{ 0 };
    uint16_t bfReserved2{ 0 };
    uint32_t bfOffBits{ 54 };
};
struct BmpInfoHeader {
    uint32_t biSize{ 40 };
    int32_t biWidth{ 0 };
    int32_t biHeight{ 0 };
    uint16_t biPlanes{ 1 };
    uint16_t biBitCount{ 24 };
    uint32_t biCompression{ 0 };
    uint32_t biSizeImage{ 0 };
    int32_t biXPelsPerMeter{ 0 };
    int32_t biYPelsPerMeter{ 0 };
    uint32_t biClrUsed{ 0 };
    uint32_t biClrImportant{ 0 };
};
#pragma pack(pop)

static void SaveNv12ToBmp(const uint8_t* nv12, int width, int height, const std::string& filepath) {
    if (!nv12 || width <= 0 || height <= 0) return;

    int rowStride = (width * 3 + 3) & ~3;
    uint32_t imageSize = rowStride * height;

    BmpFileHeader fileHeader{};
    fileHeader.bfSize = sizeof(BmpFileHeader) + sizeof(BmpInfoHeader) + imageSize;

    BmpInfoHeader infoHeader{};
    infoHeader.biWidth = width;
    infoHeader.biHeight = -height;
    infoHeader.biSizeImage = imageSize;

    std::vector<uint8_t> rgbBuf(imageSize, 0);
    const uint8_t* yPlane = nv12;
    const uint8_t* uvPlane = nv12 + (width * height);

    for (int y = 0; y < height; ++y) {
        uint8_t* row = rgbBuf.data() + (y * rowStride);
        for (int x = 0; x < width; ++x) {
            int yVal = yPlane[y * width + x] - 16;
            int uvIdx = ((y / 2) * width) + ((x / 2) * 2);
            int uVal = uvPlane[uvIdx] - 128;
            int vVal = uvPlane[uvIdx + 1] - 128;

            int c = yVal < 0 ? 0 : yVal;
            int r = std::clamp((298 * c + 409 * vVal + 128) >> 8, 0, 255);
            int g = std::clamp((298 * c - 100 * uVal - 208 * vVal + 128) >> 8, 0, 255);
            int b = std::clamp((298 * c + 516 * uVal + 128) >> 8, 0, 255);

            row[x * 3 + 0] = static_cast<uint8_t>(b);
            row[x * 3 + 1] = static_cast<uint8_t>(g);
            row[x * 3 + 2] = static_cast<uint8_t>(r);
        }
    }

    std::ofstream ofs(filepath, std::ios::binary);
    if (ofs.is_open()) {
        ofs.write(reinterpret_cast<const char*>(&fileHeader), sizeof(fileHeader));
        ofs.write(reinterpret_cast<const char*>(&infoHeader), sizeof(infoHeader));
        ofs.write(reinterpret_cast<const char*>(rgbBuf.data()), imageSize);
    }
}

// Compute 16x16 Macroblock Discontinuity Ratio from NV12
static double CalculateMacroblockDiscontinuity(const uint8_t* nv12, int width, int height) {
    if (!nv12 || width <= 32 || height <= 32) return 1.0;

    double gridDiffSum = 0.0;
    int gridCount = 0;
    double nonGridDiffSum = 0.0;
    int nonGridCount = 0;

    for (int y = 0; y < height; ++y) {
        const uint8_t* row = nv12 + (y * width);
        for (int x = 1; x < width; ++x) {
            double diff = std::abs(static_cast<int>(row[x]) - static_cast<int>(row[x - 1]));
            if (x % 16 == 0) {
                gridDiffSum += diff;
                gridCount++;
            } else if (x % 16 == 8) {
                nonGridDiffSum += diff;
                nonGridCount++;
            }
        }
    }

    double avgGrid = gridCount > 0 ? (gridDiffSum / gridCount) : 0.0;
    double avgNonGrid = nonGridCount > 0 ? (nonGridDiffSum / nonGridCount) : 1.0;
    return (avgNonGrid > 0.1) ? (avgGrid / avgNonGrid) : 1.0;
}

int main(int argc, char* argv[]) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    MFStartup(MF_VERSION);

    std::cout << "================================================================================" << std::endl;
    std::cout << "  KM VIRTUAL CAMERA: OFFLINE H.264 BITSTREAM INSPECTOR & REPLAY VALIDATOR        " << std::endl;
    std::cout << "================================================================================" << std::endl;

    std::string h264Path = "debug_stream_dump.h264";
    if (argc > 1) {
        h264Path = argv[1];
    }

    std::ifstream ifs(h264Path, std::ios::binary);
    if (!ifs.is_open()) {
        std::cerr << "[ERROR] Could not open H.264 dump file: " << h264Path << std::endl;
        std::cerr << "Run Receiver with smartphone connected first to generate dump!" << std::endl;
        return 1;
    }

    std::vector<uint8_t> fileData((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    ifs.close();

    std::cout << "Loaded dump file: " << h264Path << " (" << fileData.size() << " bytes)" << std::endl;

    struct AccessUnit {
        size_t offset = 0;
        size_t size = 0;
    };

    std::vector<AccessUnit> accessUnits;
    std::string jsonlPath = h264Path.substr(0, h264Path.find_last_of('.')) + ".jsonl";
    std::ifstream jifs(jsonlPath);
    if (jifs.is_open()) {
        std::string line;
        size_t curOffset = 0;
        while (std::getline(jifs, line)) {
            auto pos = line.find("\"size\":");
            if (pos != std::string::npos) {
                size_t frameSize = std::stoull(line.substr(pos + 7));
                if (curOffset + frameSize <= fileData.size()) {
                    AccessUnit au{};
                    au.offset = curOffset;
                    au.size = frameSize;
                    accessUnits.push_back(au);
                    curOffset += frameSize;
                }
            }
        }
    }

    if (accessUnits.empty()) {
        size_t auStart = 0;
        for (size_t i = 0; i + 4 < fileData.size(); ++i) {
            if (fileData[i] == 0 && fileData[i+1] == 0 && fileData[i+2] == 0 && fileData[i+3] == 1) {
                uint8_t naluType = fileData[i+4] & 0x1F;
                if ((naluType == 7 || naluType == 9 || naluType == 5 || naluType == 1) && i > auStart + 32) {
                    AccessUnit au{};
                    au.offset = auStart;
                    au.size = i - auStart;
                    accessUnits.push_back(au);
                    auStart = i;
                }
            }
        }
        if (auStart < fileData.size()) {
            AccessUnit au{};
            au.offset = auStart;
            au.size = fileData.size() - auStart;
            accessUnits.push_back(au);
        }
    }

    std::cout << "Found " << accessUnits.size() << " Access Units in stream dump." << std::endl;

    // Initialize MFT decoder
    km::codec::H264Decoder decoder;
    if (!decoder.Initialize(1280, 720, nullptr)) {
        std::cerr << "[ERROR] Failed to initialize MFT decoder!" << std::endl;
        return 1;
    }

    std::vector<uint8_t> nv12Buffer;
    int successCount = 0;
    int failCount = 0;
    double maxDiscontinuity = 0.0;

    std::cout << "\n[DECODING & INSPECTION PROGRESS]" << std::endl;
    std::cout << "--------------------------------------------------------------------------------" << std::endl;
    std::cout << " AU#  | Size(B) | NALUs            | Decoded Res | MB Discontinuity | Result    " << std::endl;
    std::cout << "--------------------------------------------------------------------------------" << std::endl;

    for (size_t idx = 0; idx < accessUnits.size(); ++idx) {
        const auto& au = accessUnits[idx];
        const uint8_t* auData = fileData.data() + au.offset;
        size_t auSize = au.size;

        // Scan NALUs in this AU
        std::string naluStr;
        for (size_t i = 0; i + 4 < auSize; ++i) {
            if (auData[i] == 0 && auData[i+1] == 0 && auData[i+2] == 0 && auData[i+3] == 1) {
                uint8_t t = auData[i+4] & 0x1F;
                if (!naluStr.empty()) naluStr += ",";
                naluStr += std::to_string(t);
            }
        }

        int decW = 0, decH = 0;
        int64_t tsUs = static_cast<int64_t>(idx) * 33333;
        bool ok = decoder.DecodeAccessUnit(auData, auSize, tsUs, nv12Buffer, decW, decH);

        double metric = 1.0;
        if (ok && decW > 0 && decH > 0) {
            metric = CalculateMacroblockDiscontinuity(nv12Buffer.data(), decW, decH);
            maxDiscontinuity = (std::max)(maxDiscontinuity, metric);
            successCount++;

            if (idx % 10 == 0 || idx == accessUnits.size() - 1) {
                std::string snapName = "test_screenshots/inspected_frame_" + std::to_string(idx) + ".bmp";
                SaveNv12ToBmp(nv12Buffer.data(), decW, decH, snapName);
            }
        } else {
            failCount++;
        }

        if (idx < 20 || idx % 30 == 0 || !ok || metric > 1.35) {
            std::cout << std::setw(5) << idx << " | "
                      << std::setw(7) << auSize << " | "
                      << std::setw(16) << naluStr.substr(0, 16) << " | "
                      << std::setw(5) << decW << "x" << std::setw(4) << decH << " | "
                      << std::fixed << std::setprecision(3) << std::setw(16) << metric << " | "
                      << (ok ? (metric > 1.35 ? "WARN: NOISE" : "OK") : "FAIL") << std::endl;
        }
    }

    std::cout << "--------------------------------------------------------------------------------" << std::endl;
    std::cout << "\n[SUMMARY]" << std::endl;
    std::cout << "  - Total Access Units: " << accessUnits.size() << std::endl;
    std::cout << "  - Decoded Successfully: " << successCount << std::endl;
    std::cout << "  - Failed: " << failCount << std::endl;
    std::cout << "  - Max Macroblock Discontinuity Ratio: " << maxDiscontinuity << " (Threshold: <1.35)" << std::endl;

    if (maxDiscontinuity <= 1.35 && failCount == 0) {
        std::cout << "\n>>> VERIFICATION PASSED: ZERO BLOCK NOISE DETECTED IN BITSTREAM <<<" << std::endl;
        return 0;
    } else {
        std::cerr << "\n>>> VERIFICATION FAILED: ARTIFACTS OR DECODE FAILURES DETECTED <<<" << std::endl;
        return 1;
    }
}
