#pragma once

#include <vector>
#include <cstdint>
#include <cstddef>
#include <chrono>
#include "frame_pipe_protocol.h"

namespace km::media {

class TestPatternGenerator {
public:
    TestPatternGenerator();
    ~TestPatternGenerator() = default;

    // Generate next 1280x720 NV12 frame
    void GenerateFrame(std::vector<uint8_t>& outNv12, uint64_t frameIndex, int64_t timestampUs);

    // Static helper to generate a deterministic calibration pattern for automated testing
    static void GenerateCalibrationPattern(std::vector<uint8_t>& outNv12, uint32_t patternId = 0);

private:
    std::vector<uint8_t> baseBarsY_;
    std::vector<uint8_t> baseBarsUV_;
};

} // namespace km::media
