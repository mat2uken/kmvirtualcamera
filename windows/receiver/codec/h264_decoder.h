#pragma once

#include <mfapi.h>
#include <mftransform.h>
#include <mfobjects.h>
#include <mferror.h>
#include <wrl/client.h>
#include <vector>
#include <cstdint>
#include <iostream>

namespace km::codec {

class H264Decoder {
public:
    H264Decoder();
    ~H264Decoder();

    bool Initialize(int width = 1280, int height = 720);
    void Shutdown();

    // Decodes an Annex-B H.264 Access Unit (NAL units with 00 00 00 01 start code) into NV12
    // NOTE: Must only be called from a single thread (VideoWorkerProc)
    bool DecodeAccessUnit(
        const uint8_t* h264Data,
        size_t size,
        int64_t timestampUs,
        std::vector<uint8_t>& outNv12,
        int& outWidth,
        int& outHeight
    );

    bool IsInitialized() const { return isInitialized_; }
    void GetDecodedResolution(int& width, int& height) const {
        width = actualWidth_;
        height = actualHeight_;
    }

private:
    HRESULT CreateInputType(int width, int height, IMFMediaType** ppType);
    HRESULT ConfigureOutputType(int width, int height);
    bool ExtractSampleNv12(IMFSample* pSample, std::vector<uint8_t>& outNv12, int& outW, int& outH);

    bool isInitialized_{false};
    int targetWidth_{1280};
    int targetHeight_{720};
    int actualWidth_{1280};
    int actualHeight_{720};
    int displayWidth_{1280};
    int displayHeight_{720};
    int64_t sampleIndex_{0};

    Microsoft::WRL::ComPtr<IMFTransform> decoderMft_;
    Microsoft::WRL::ComPtr<IMFSample> inSample_;
    Microsoft::WRL::ComPtr<IMFMediaBuffer> inBuffer_;
    DWORD inBufferCapacity_{0};

    Microsoft::WRL::ComPtr<IMFSample> outSample_;
    Microsoft::WRL::ComPtr<IMFMediaBuffer> outBuffer_;
    DWORD outBufferCapacity_{0};
};

} // namespace km::codec
