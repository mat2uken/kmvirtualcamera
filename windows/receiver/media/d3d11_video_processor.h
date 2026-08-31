#pragma once

#include <windows.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <wrl/client.h>
#include <cstdint>
#include <algorithm>
#include <iostream>
#include "../../common/frame_pipe_protocol.h"

namespace km::media {

class D3D11VideoProcessor {
public:
    D3D11VideoProcessor() = default;
    ~D3D11VideoProcessor() { Release(); }

    bool Initialize(ID3D11Device* pDevice, UINT dstWidth = protocol::kWidth, UINT dstHeight = protocol::kHeight);

    // Hardware GPU Video Processing: Scaling, Letterboxing, Color Range, Background Fill (<0.05ms)
    bool ProcessVideoFrame(
        ID3D11DeviceContext* pContext,
        ID3D11Texture2D* pSrcTexture,
        UINT srcSubresource,
        UINT srcWidth,
        UINT srcHeight,
        ID3D11Texture2D* pDstTexture,
        UINT dstWidth,
        UINT dstHeight,
        int rotationDegrees = 0
    );

    bool IsSupported() const { return isSupported_; }
    void Release();

private:
    bool EnsureVideoProcessor(UINT srcWidth, UINT srcHeight, UINT dstWidth, UINT dstHeight);

    bool isInitialized_{false};
    bool isSupported_{false};
    UINT currentSrcW_{0};
    UINT currentSrcH_{0};
    UINT currentDstW_{protocol::kWidth};
    UINT currentDstH_{protocol::kHeight};

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11VideoDevice> videoDevice_;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorEnumerator> enumerator_;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessor> videoProcessor_;
    D3D11_VIDEO_PROCESSOR_CAPS vpCaps_{};
};

} // namespace km::media
