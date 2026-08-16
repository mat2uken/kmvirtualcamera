#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <cassert>

static int getNumRawDataModules(int version) {
    int result = (16 * version + 128) * version + 64;
    if (version >= 2) {
        int numAlign = version / 7 + 2;
        result -= (25 * numAlign - 10) * numAlign - 55;
        if (version >= 7)
            result -= 36;
    }
    return result;
}

int main() {
    std::cout << "Version 1 raw modules: " << getNumRawDataModules(1) << " (codewords: " << getNumRawDataModules(1)/8 << ")" << std::endl;
    std::cout << "Version 7 raw modules: " << getNumRawDataModules(7) << " (codewords: " << getNumRawDataModules(7)/8 << ")" << std::endl;
    return 0;
}
