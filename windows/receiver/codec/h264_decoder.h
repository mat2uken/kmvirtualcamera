#pragma once

#include <windows.h>
#include <mfapi.h>
#include <mftransform.h>
#include <mfobjects.h>
#include <mferror.h>
#include <dxgi.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <vector>
#include <cstdint>
#include <iostream>
#include <atomic>

namespace km::codec {

struct GpuDecodedFrame {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    UINT subresourceIndex = 0;
    int codedWidth = 0;
    int codedHeight = 0;
    int displayWidth = 0;
    int displayHeight = 0;
};

class H264Decoder {
public:
    H264Decoder();
    ~H264Decoder();

    bool Initialize(int width = 1280, int height = 720, ID3D11Device* pD3DDevice = nullptr);
    void Shutdown();

    // Decodes an Annex-B H.264 Access Unit (NAL units with 00 00 00 01 start code).
    // If hardware D3D11 decoding is active and returns a GPU texture, outIsGpuDirect will be true
    // and outGpuFrame will contain the decoded ID3D11Texture2D (Zero-Copy).
    // Otherwise, outIsGpuDirect will be false and outCpuNv12 will contain the CPU NV12 bytes.
    bool DecodeAccessUnitEx(
        const uint8_t* h264Data,
        size_t size,
        int64_t timestampUs,
        GpuDecodedFrame& outGpuFrame,
        std::vector<uint8_t>& outCpuNv12,
        bool& outIsGpuDirect
    );

    // Legacy CPU-only signature for compatibility
    bool DecodeAccessUnit(
        const uint8_t* h264Data,
        size_t size,
        int64_t timestampUs,
        std::vector<uint8_t>& outNv12,
        int& outWidth,
        int& outHeight
    );

    bool IsInitialized() const { return isInitialized_; }
    bool IsHardwareAccelerated() const { return isHardwareAccelerated_; }

    // Shutdown diagnostics: which internal call the decode is currently inside
    // (0 = idle, 100-139 = stage markers set around MF/D3D11 calls in
    // h264_decoder.cpp). Read by the app shutdown watchdog while a join stalls.
    static std::atomic<int>& DebugStage() { static std::atomic<int> stage{0}; return stage; }
    void GetDecodedResolution(int& width, int& height) const {
        width = actualWidth_;
        height = actualHeight_;
    }

private:
    HRESULT CreateInputType(int width, int height, IMFMediaType** ppType);
    HRESULT ConfigureOutputType(int width, int height);
    bool ExtractSampleNv12(IMFSample* pSample, std::vector<uint8_t>& outNv12, int& outW, int& outH);

    bool isInitialized_{false};
    bool isHardwareAccelerated_{false};
    int targetWidth_{1280};
    int targetHeight_{720};
    int actualWidth_{1280};
    int actualHeight_{720};
    int displayWidth_{1280};
    int displayHeight_{720};
    int64_t sampleIndex_{0};

    Microsoft::WRL::ComPtr<ID3D11Device> d3dDevice_;
    Microsoft::WRL::ComPtr<IMFDXGIDeviceManager> dxgiDeviceManager_;
    UINT dxgiResetToken_{0};

    Microsoft::WRL::ComPtr<IMFTransform> decoderMft_;
    Microsoft::WRL::ComPtr<IMFSample> inSample_;
    Microsoft::WRL::ComPtr<IMFMediaBuffer> inBuffer_;
    DWORD inBufferCapacity_{0};

    Microsoft::WRL::ComPtr<IMFSample> outSample_;
    Microsoft::WRL::ComPtr<IMFMediaBuffer> outBuffer_;
    DWORD outBufferCapacity_{0};
};

} // namespace km::codec
