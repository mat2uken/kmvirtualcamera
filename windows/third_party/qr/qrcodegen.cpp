/* 
 * QR Code generator library (C++)
 * 
 * Copyright (c) Project Nayuki. (MIT License)
 * https://www.nayuki.io/page/qr-code-generator-library
 */

#include "qrcodegen.hpp"
#include <algorithm>
#include <climits>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <utility>

using std::int8_t;
using std::uint8_t;
using std::size_t;
using std::vector;

namespace qrcodegen {

QrSegment::QrSegment(Mode md, int numCh, const vector<bool> &dt) :
    mode(md),
    numChars(numCh),
    data(dt) {
    if (numCh < 0)
        throw std::domain_error("Invalid value");
}

QrSegment::QrSegment(Mode md, int numCh, vector<bool> &&dt) :
    mode(md),
    numChars(numCh),
    data(std::move(dt)) {
    if (numCh < 0)
        throw std::domain_error("Invalid value");
}

QrSegment::Mode QrSegment::getMode() const {
    return mode;
}

int QrSegment::getNumChars() const {
    return numChars;
}

const vector<bool> &QrSegment::getData() const {
    return data;
}

QrSegment QrSegment::makeBytes(const vector<uint8_t> &data) {
    if (data.size() > static_cast<unsigned int>(INT_MAX))
        throw std::length_error("Data too long");
    vector<bool> bb;
    for (uint8_t b : data) {
        for (int i = 7; i >= 0; i--)
            bb.push_back((b >> i) & 1);
    }
    return QrSegment(Mode::BYTE, static_cast<int>(data.size()), std::move(bb));
}

QrSegment QrSegment::makeNumeric(const char *digits) {
    size_t len = std::strlen(digits);
    vector<bool> bb;
    for (size_t i = 0; i < len; ) {
        int n = 0;
        int count = std::min(static_cast<size_t>(3), len - i);
        for (int j = 0; j < count; j++, i++) {
            char c = digits[i];
            if (c < '0' || c > '9')
                throw std::domain_error("String contains non-numeric characters");
            n = n * 10 + (c - '0');
        }
        int bits = count * 3 + 1;
        for (int j = bits - 1; j >= 0; j--)
            bb.push_back((n >> j) & 1);
    }
    return QrSegment(Mode::NUMERIC, static_cast<int>(len), std::move(bb));
}

static const char *ALPHANUMERIC_CHARSET = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ $%*+-./:";

QrSegment QrSegment::makeAlphanumeric(const char *text) {
    size_t len = std::strlen(text);
    vector<bool> bb;
    for (size_t i = 0; i < len; ) {
        int temp = 0;
        int count = std::min(static_cast<size_t>(2), len - i);
        for (int j = 0; j < count; j++, i++) {
            const char *p = std::strchr(ALPHANUMERIC_CHARSET, text[i]);
            if (p == nullptr)
                throw std::domain_error("String contains unencodable characters in alphanumeric mode");
            temp = temp * 45 + static_cast<int>(p - ALPHANUMERIC_CHARSET);
        }
        int bits = (count == 2) ? 11 : 6;
        for (int j = bits - 1; j >= 0; j--)
            bb.push_back((temp >> j) & 1);
    }
    return QrSegment(Mode::ALPHANUMERIC, static_cast<int>(len), std::move(bb));
}

vector<QrSegment> QrSegment::makeSegments(const char *text) {
    if (text[0] == '\0')
        return vector<QrSegment>();
    vector<uint8_t> bytes;
    for (; *text != '\0'; text++)
        bytes.push_back(static_cast<uint8_t>(*text));
    return { QrSegment::makeBytes(bytes) };
}

int QrSegment::getTotalBits(const vector<QrSegment> &segs, int version) {
    int result = 0;
    for (const QrSegment &seg : segs) {
        int ccbits;
        switch (seg.mode) {
            case Mode::NUMERIC:      ccbits = (version < 10) ? 10 : (version < 27) ? 12 : 14; break;
            case Mode::ALPHANUMERIC: ccbits = (version < 10) ?  9 : (version < 27) ? 11 : 13; break;
            case Mode::BYTE:         ccbits = (version < 10) ?  8 : 16; break;
            case Mode::KANJI:        ccbits = (version < 10) ?  8 : (version < 27) ? 10 : 12; break;
            case Mode::ECI:          ccbits = 0; break;
            default:  throw std::logic_error("Unreachable");
        }
        if (seg.numChars >= (1 << ccbits))
            return -1;
        result += 4 + ccbits + static_cast<int>(seg.data.size());
    }
    return result;
}

static const int ECC_CODEWORDS_PER_BLOCK[4][41] = {
    {-1,  7, 10, 15, 20, 26, 18, 20, 24, 30, 18, 20, 24, 26, 30, 22, 24, 28, 30, 28, 28, 28, 28, 30, 30, 26, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30},
    {-1, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26, 30, 22, 22, 24, 24, 28, 28, 26, 26, 26, 26, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28},
    {-1, 13, 22, 18, 26, 18, 24, 18, 22, 20, 24, 28, 26, 24, 20, 30, 24, 28, 28, 26, 30, 28, 30, 30, 30, 30, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30},
    {-1, 17, 28, 22, 16, 22, 28, 26, 26, 24, 28, 24, 28, 22, 24, 24, 30, 28, 28, 26, 28, 30, 24, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30},
};

static const int NUM_ERROR_CORRECTION_BLOCKS[4][41] = {
    {-1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 4, 4, 4, 4, 4, 6, 8, 8, 8, 8, 10, 12, 12, 12, 12, 16, 16, 16, 16, 18, 20, 20, 22, 24, 24, 26, 28, 28, 32, 34, 36},
    {-1, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5, 5, 8, 9, 9, 10, 10, 11, 13, 14, 16, 17, 17, 18, 20, 21, 23, 25, 26, 28, 29, 31, 33, 35, 37, 38, 40, 43, 45, 47, 49},
    {-1, 1, 1, 2, 2, 4, 4, 6, 6, 8, 8, 8, 10, 12, 16, 12, 17, 16, 18, 21, 20, 23, 23, 25, 27, 29, 34, 34, 35, 38, 40, 43, 45, 48, 51, 53, 56, 59, 62, 65, 68},
    {-1, 1, 1, 2, 4, 4, 4, 5, 6, 8, 8, 11, 11, 16, 16, 18, 16, 19, 21, 25, 25, 25, 34, 30, 32, 35, 37, 40, 42, 45, 48, 51, 54, 57, 60, 63, 66, 70, 72, 74, 79},
};

static int getNumDataCodewords(int version, QrCode::Ecc ecl) {
    int v = version, e = static_cast<int>(ecl);
    int total = (16 * v * v + 128 * v + 137) / 8;
    int reserved = 0;
    if (v >= 2) reserved += (4 * ((v / 7) + 2) - 3) * 25 - 60;
    if (v >= 7) reserved += 36;
    total = (total - reserved) * 8 / 8;
    return total - ECC_CODEWORDS_PER_BLOCK[e][v] * NUM_ERROR_CORRECTION_BLOCKS[e][v];
}

QrCode QrCode::encodeText(const char *text, Ecc ecl) {
    vector<QrSegment> segs = QrSegment::makeSegments(text);
    return encodeSegments(segs, ecl);
}

QrCode QrCode::encodeSegments(const vector<QrSegment> &segs, Ecc ecl, int minVersion, int maxVersion, int mask, bool boostEcl) {
    if (minVersion < 1 || minVersion > maxVersion || maxVersion > 40 || mask < -1 || mask > 7)
        throw std::invalid_argument("Invalid value");

    int version = minVersion;
    int dataUsedBits = -1;
    for (; version <= maxVersion; version++) {
        int dataCapacityBits = getNumDataCodewords(version, ecl) * 8;
        dataUsedBits = QrSegment::getTotalBits(segs, version);
        if (dataUsedBits != -1 && dataUsedBits <= dataCapacityBits)
            break;
    }
    if (version > maxVersion)
        throw std::length_error("Data too long for specified QR Code versions");

    if (boostEcl) {
        for (Ecc newEcl : {Ecc::MEDIUM, Ecc::QUARTILE, Ecc::HIGH}) {
            if (dataUsedBits <= getNumDataCodewords(version, newEcl) * 8)
                ecl = newEcl;
        }
    }

    vector<bool> bb;
    for (const QrSegment &seg : segs) {
        int modeBits = static_cast<int>(seg.getMode());
        for (int i = 3; i >= 0; i--) bb.push_back((modeBits >> i) & 1);
        int ccbits = (seg.getMode() == QrSegment::Mode::BYTE) ? ((version < 10) ? 8 : 16) : ((version < 10) ? 9 : 11);
        for (int i = ccbits - 1; i >= 0; i--) bb.push_back((seg.getNumChars() >> i) & 1);
        for (bool b : seg.getData()) bb.push_back(b);
    }

    int capacityBits = getNumDataCodewords(version, ecl) * 8;
    for (int i = 0; i < 4 && static_cast<int>(bb.size()) < capacityBits; i++) bb.push_back(false);
    while (bb.size() % 8 != 0) bb.push_back(false);
    for (uint8_t pad = 0xEC; bb.size() < static_cast<size_t>(capacityBits); pad ^= 0xEC ^ 0x11) {
        for (int i = 7; i >= 0; i--) bb.push_back((pad >> i) & 1);
    }

    vector<uint8_t> bytes(bb.size() / 8);
    for (size_t i = 0; i < bb.size(); i++) bytes[i / 8] |= (bb[i] ? 1 : 0) << (7 - (i % 8));

    return QrCode(version, ecl, bytes, mask);
}

QrCode::QrCode(int ver, Ecc ecl, const vector<uint8_t> &dataCodewords, int msk) :
    version(ver),
    size(ver * 4 + 17),
    errorCorrectionLevel(ecl),
    modules(size, vector<bool>(size)),
    isFunction(size, vector<bool>(size)) {

    drawFunctionPatterns();
    vector<uint8_t> allCodewords = getPayload(dataCodewords);

    // Draw data codewords
    size_t i = 0;
    for (int right = size - 1; right >= 1; right -= 2) {
        if (right == 6) right = 5;
        for (int vert = 0; vert < size; vert++) {
            for (int j = 0; j < 2; j++) {
                int x = right - j;
                bool upward = ((right + 1) & 2) == 0;
                int y = upward ? size - 1 - vert : vert;
                if (!isFunction[y][x] && i < allCodewords.size() * 8) {
                    modules[y][x] = (allCodewords[i / 8] >> (7 - (i % 8))) & 1;
                    i++;
                }
            }
        }
    }

    if (msk == -1) {
        long minPenalty = LONG_MAX;
        for (int m = 0; m < 8; m++) {
            applyMask(m);
            drawFormatBits(m);
            long penalty = getPenaltyScore();
            if (penalty < minPenalty) {
                msk = m;
                minPenalty = penalty;
            }
            applyMask(m);
        }
    }
    mask = msk;
    applyMask(msk);
    drawFormatBits(msk);
}

int QrCode::getVersion() const { return version; }
int QrCode::getSize() const { return size; }
QrCode::Ecc QrCode::getErrorCorrectionLevel() const { return errorCorrectionLevel; }
int QrCode::getMask() const { return mask; }
bool QrCode::getModule(int x, int y) const { return (0 <= x && x < size && 0 <= y && y < size) && modules[y][x]; }

void QrCode::drawFunctionPatterns() {
    for (int i = 0; i < size; i++) {
        setFunctionModule(6, i, i % 2 == 0);
        setFunctionModule(i, 6, i % 2 == 0);
    }
    drawFinderPattern(3, 3);
    drawFinderPattern(size - 4, 3);
    drawFinderPattern(3, size - 4);
    drawVersion();
}

void QrCode::drawFinderPattern(int x, int y) {
    for (int dy = -4; dy <= 4; dy++) {
        for (int dx = -4; dx <= 4; dx++) {
            int dist = std::max(std::abs(dx), std::abs(dy));
            int xx = x + dx, yy = y + dy;
            if (0 <= xx && xx < size && 0 <= yy && yy < size)
                setFunctionModule(xx, yy, dist != 2 && dist != 4);
        }
    }
}

void QrCode::drawVersion() {
    if (version < 7) return;
    int rem = version;
    for (int i = 0; i < 12; i++) rem = (rem << 1) ^ ((rem >> 11) * 0x1F25);
    long bits = static_cast<long>(version) << 12 | rem;
    for (int i = 0; i < 18; i++) {
        bool bit = (bits >> i) & 1;
        int a = size - 11 + i % 3, b = i / 3;
        setFunctionModule(a, b, bit);
        setFunctionModule(b, a, bit);
    }
}

void QrCode::drawFormatBits(int msk) {
    int data = static_cast<int>(errorCorrectionLevel) ^ 1;
    data = (data << 3) | msk;
    int rem = data;
    for (int i = 0; i < 10; i++) rem = (rem << 1) ^ ((rem >> 9) * 0x537);
    int bits = (data << 10 | rem) ^ 0x5412;
    for (int i = 0; i <= 5; i++) setFunctionModule(8, i, (bits >> i) & 1);
    setFunctionModule(8, 7, (bits >> 6) & 1);
    setFunctionModule(8, 8, (bits >> 7) & 1);
    setFunctionModule(7, 8, (bits >> 8) & 1);
    for (int i = 9; i < 15; i++) setFunctionModule(14 - i, 8, (bits >> i) & 1);
    for (int i = 0; i < 8; i++) setFunctionModule(size - 1 - i, 8, (bits >> i) & 1);
    for (int i = 8; i < 15; i++) setFunctionModule(8, size - 15 + i, (bits >> i) & 1);
    setFunctionModule(8, size - 8, true);
}

void QrCode::setFunctionModule(int x, int y, bool isBlack) {
    modules[y][x] = isBlack;
    isFunction[y][x] = true;
}

static uint8_t reedSolomonMultiply(uint8_t x, uint8_t y) {
    int z = 0;
    for (int i = 7; i >= 0; i--) {
        z = (z << 1) ^ ((z >> 8) * 0x11D);
        z ^= ((y >> i) & 1) * x;
    }
    return static_cast<uint8_t>(z);
}

static vector<uint8_t> reedSolomonComputeDivisor(int degree) {
    vector<uint8_t> result(degree);
    result[degree - 1] = 1;
    uint8_t root = 1;
    for (int i = 0; i < degree; i++) {
        for (size_t j = 0; j < result.size(); j++) {
            result[j] = reedSolomonMultiply(result[j], root);
            if (j + 1 < result.size()) result[j] ^= result[j + 1];
        }
        root = reedSolomonMultiply(root, 0x02);
    }
    return result;
}

static vector<uint8_t> reedSolomonComputeRemainder(const vector<uint8_t> &data, const vector<uint8_t> &divisor) {
    vector<uint8_t> result(divisor.size());
    for (uint8_t b : data) {
        uint8_t factor = b ^ result[0];
        result.erase(result.begin());
        result.push_back(0);
        for (size_t i = 0; i < divisor.size(); i++)
            result[i] ^= reedSolomonMultiply(divisor[i], factor);
    }
    return result;
}

vector<uint8_t> QrCode::getPayload(const vector<uint8_t> &dataCodewords) const {
    int numBlocks = NUM_ERROR_CORRECTION_BLOCKS[static_cast<int>(errorCorrectionLevel)][version];
    int blockEccLen = ECC_CODEWORDS_PER_BLOCK[static_cast<int>(errorCorrectionLevel)][version];
    int rawCodewords = (16 * version * version + 128 * version + 137) / 8;
    int numShortBlocks = numBlocks - rawCodewords % numBlocks;
    int shortBlockLen = rawCodewords / numBlocks;

    vector<vector<uint8_t>> blocks;
    vector<uint8_t> rsDivisor = reedSolomonComputeDivisor(blockEccLen);
    for (int i = 0, k = 0; i < numBlocks; i++) {
        int datLen = shortBlockLen - blockEccLen + (i >= numShortBlocks ? 1 : 0);
        vector<uint8_t> dat(dataCodewords.begin() + k, dataCodewords.begin() + (k + datLen));
        k += datLen;
        vector<uint8_t> ecc = reedSolomonComputeRemainder(dat, rsDivisor);
        dat.insert(dat.end(), ecc.begin(), ecc.end());
        blocks.push_back(dat);
    }

    vector<uint8_t> result;
    for (size_t i = 0; i < blocks[0].size(); i++) {
        for (size_t j = 0; j < blocks.size(); j++) {
            if (i < blocks[j].size()) result.push_back(blocks[j][i]);
        }
    }
    return result;
}

void QrCode::applyMask(int msk) {
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            if (isFunction[y][x]) continue;
            bool invert;
            switch (msk) {
                case 0:  invert = (x + y) % 2 == 0; break;
                case 1:  invert = y % 2 == 0; break;
                case 2:  invert = x % 3 == 0; break;
                case 3:  invert = (x + y) % 3 == 0; break;
                case 4:  invert = (x / 3 + y / 2) % 2 == 0; break;
                case 5:  invert = (x * y) % 2 + (x * y) % 3 == 0; break;
                case 6:  invert = ((x * y) % 2 + (x * y) % 3) % 2 == 0; break;
                case 7:  invert = ((x + y) % 2 + (x * y) % 3) % 2 == 0; break;
                default: throw std::logic_error("Unreachable");
            }
            modules[y][x] = modules[y][x] ^ invert;
        }
    }
}

long QrCode::getPenaltyScore() const {
    long result = 0;
    for (int y = 0; y < size; y++) {
        bool runColor = false;
        int runX = 0;
        for (int x = 0; x < size; x++) {
            if (modules[y][x] == runColor) {
                runX++;
                if (runX == 5) result += 3;
                else if (runX > 5) result += 1;
            } else {
                runColor = modules[y][x];
                runX = 1;
            }
        }
    }
    return result;
}

}
