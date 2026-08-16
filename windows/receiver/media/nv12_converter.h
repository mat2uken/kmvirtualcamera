#pragma once

#include "../../common/frame_pipe_protocol.h"
#include <vector>
#include <cstdint>

namespace km::media {

class Nv12Converter {
public:
    Nv12Converter();
    ~Nv12Converter() = default;

    // Converts I420 source image (srcY, srcU, srcV) to 1280x720 NV12 buffer with aspect-fit letterbox.
    void ConvertI420ToNv12(
        const uint8_t* srcY, int srcStrideY,
        const uint8_t* srcU, int srcStrideU,
        const uint8_t* srcV, int srcStrideV,
        int srcWidth, int srcHeight,
        uint8_t* dstNv12,
        int dstWidth = 1280, int dstHeight = 720
    );

    // Converts RGB/BGR 24/32-bit frame to 1280x720 NV12 with aspect-fit letterbox.
    void ConvertBgraToNv12(
        const uint8_t* srcBgra,
        int srcWidth, int srcHeight, int srcStride,
        uint8_t* dstNv12,
        int dstWidth = 1280, int dstHeight = 720
    );
};

} // namespace km::media
