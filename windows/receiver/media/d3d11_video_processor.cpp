#include "d3d11_video_processor.h"

namespace km::media {

bool D3D11VideoProcessor::Initialize(ID3D11Device* pDevice, UINT dstWidth, UINT dstHeight) {
    if (!pDevice) return false;
    Release();

    device_ = pDevice;
    currentDstW_ = dstWidth > 0 ? dstWidth : protocol::kWidth;
    currentDstH_ = dstHeight > 0 ? dstHeight : protocol::kHeight;

    HRESULT hr = device_.As(&videoDevice_);
    if (FAILED(hr) || !videoDevice_) {
        std::cerr << "[D3D11VideoProcessor] ID3D11VideoDevice interface not supported on device." << std::endl;
        isSupported_ = false;
        return false;
    }

    isSupported_ = true;
    isInitialized_ = true;
    return true;
}

bool D3D11VideoProcessor::EnsureVideoProcessor(UINT srcWidth, UINT srcHeight, UINT dstWidth, UINT dstHeight) {
    if (!videoDevice_) return false;
    if (videoProcessor_ && enumerator_ && currentSrcW_ == srcWidth && currentSrcH_ == srcHeight &&
        currentDstW_ == dstWidth && currentDstH_ == dstHeight) {
        return true;
    }

    videoProcessor_.Reset();
    enumerator_.Reset();

    currentSrcW_ = srcWidth;
    currentSrcH_ = srcHeight;
    currentDstW_ = dstWidth;
    currentDstH_ = dstHeight;

    D3D11_VIDEO_PROCESSOR_CONTENT_DESC contentDesc{};
    contentDesc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    contentDesc.InputFrameRate.Numerator = 60;
    contentDesc.InputFrameRate.Denominator = 1;
    contentDesc.InputWidth = currentSrcW_;
    contentDesc.InputHeight = currentSrcH_;
    contentDesc.OutputWidth = currentDstW_;
    contentDesc.OutputHeight = currentDstH_;
    contentDesc.OutputFrameRate.Numerator = 60;
    contentDesc.OutputFrameRate.Denominator = 1;
    contentDesc.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;

    HRESULT hr = videoDevice_->CreateVideoProcessorEnumerator(&contentDesc, &enumerator_);
    if (FAILED(hr) || !enumerator_) {
        std::cerr << "[D3D11VideoProcessor] CreateVideoProcessorEnumerator failed: hr=0x" << std::hex << hr << std::dec << std::endl;
        return false;
    }

    hr = enumerator_->GetVideoProcessorCaps(&vpCaps_);
    if (FAILED(hr)) {
        std::cerr << "[D3D11VideoProcessor] GetVideoProcessorCaps failed: hr=0x" << std::hex << hr << std::dec << std::endl;
        return false;
    }

    hr = videoDevice_->CreateVideoProcessor(enumerator_.Get(), 0, &videoProcessor_);
    if (FAILED(hr) || !videoProcessor_) {
        std::cerr << "[D3D11VideoProcessor] CreateVideoProcessor failed: hr=0x" << std::hex << hr << std::dec << std::endl;
        return false;
    }

    return true;
}

bool D3D11VideoProcessor::ProcessVideoFrame(
    ID3D11DeviceContext* pContext,
    ID3D11Texture2D* pSrcTexture,
    UINT srcSubresource,
    UINT srcWidth,
    UINT srcHeight,
    ID3D11Texture2D* pDstTexture,
    UINT dstWidth,
    UINT dstHeight,
    int rotationDegrees
) {
    if (!isInitialized_ || !videoDevice_ || !pContext || !pSrcTexture || !pDstTexture) return false;
    if (srcWidth == 0 || srcHeight == 0 || dstWidth == 0 || dstHeight == 0) return false;

    Microsoft::WRL::ComPtr<ID3D11VideoContext> videoContext;
    HRESULT hr = pContext->QueryInterface(IID_PPV_ARGS(&videoContext));
    if (FAILED(hr) || !videoContext) {
        return false;
    }

    if (!EnsureVideoProcessor(srcWidth, srcHeight, dstWidth, dstHeight)) {
        return false;
    }

    // 1. Create Input View for Source Texture
    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inViewDesc{};
    inViewDesc.FourCC = 0;
    inViewDesc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
    inViewDesc.Texture2D.MipSlice = 0;
    inViewDesc.Texture2D.ArraySlice = srcSubresource;

    Microsoft::WRL::ComPtr<ID3D11VideoProcessorInputView> inputView;
    hr = videoDevice_->CreateVideoProcessorInputView(pSrcTexture, enumerator_.Get(), &inViewDesc, &inputView);
    if (FAILED(hr) || !inputView) {
        return false;
    }

    // 2. Create Output View for Destination Shared Texture
    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC outViewDesc{};
    outViewDesc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
    outViewDesc.Texture2D.MipSlice = 0;

    Microsoft::WRL::ComPtr<ID3D11VideoProcessorOutputView> outputView;
    hr = videoDevice_->CreateVideoProcessorOutputView(pDstTexture, enumerator_.Get(), &outViewDesc, &outputView);
    if (FAILED(hr) || !outputView) {
        return false;
    }

    // 3. Configure Studio Black Background (Y=16, U=128, V=128 in normalized float [0.0, 1.0])
    D3D11_VIDEO_COLOR bgColor{};
    bgColor.YCbCr.Y = 16.0f / 255.0f;
    bgColor.YCbCr.Cb = 128.0f / 255.0f;
    bgColor.YCbCr.Cr = 128.0f / 255.0f;
    bgColor.YCbCr.A = 1.0f;
    videoContext->VideoProcessorSetOutputBackgroundColor(videoProcessor_.Get(), FALSE, &bgColor);

    // 4. Calculate Aspect-Ratio Fit Rectangle (Letterboxing)
    float srcAspect = static_cast<float>(srcWidth) / static_cast<float>(srcHeight);
    float dstAspect = static_cast<float>(dstWidth) / static_cast<float>(dstHeight);

    int fitW = static_cast<int>(dstWidth);
    int fitH = static_cast<int>(dstHeight);
    if (srcAspect > dstAspect) {
        fitH = static_cast<int>(static_cast<float>(dstWidth) / srcAspect);
    } else {
        fitW = static_cast<int>(static_cast<float>(dstHeight) * srcAspect);
    }

    fitW = (fitW / 2) * 2;
    fitH = (fitH / 2) * 2;
    if (fitW <= 0) fitW = 2;
    if (fitH <= 0) fitH = 2;

    int offsetX = ((static_cast<int>(dstWidth) - fitW) / 4) * 2;
    int offsetY = ((static_cast<int>(dstHeight) - fitH) / 4) * 2;

    RECT srcRect{ 0, 0, static_cast<LONG>(srcWidth), static_cast<LONG>(srcHeight) };
    RECT dstRect{ static_cast<LONG>(offsetX), static_cast<LONG>(offsetY),
                  static_cast<LONG>(offsetX + fitW), static_cast<LONG>(offsetY + fitH) };
    RECT targetRect{ 0, 0, static_cast<LONG>(dstWidth), static_cast<LONG>(dstHeight) };

    videoContext->VideoProcessorSetStreamSourceRect(videoProcessor_.Get(), 0, TRUE, &srcRect);
    videoContext->VideoProcessorSetStreamDestRect(videoProcessor_.Get(), 0, TRUE, &dstRect);
    videoContext->VideoProcessorSetOutputTargetRect(videoProcessor_.Get(), TRUE, &targetRect);

    // 5. Execute Hardware Video Processor Blt
    D3D11_VIDEO_PROCESSOR_STREAM stream{};
    stream.Enable = TRUE;
    stream.OutputIndex = 0;
    stream.InputFrameOrField = 0;
    stream.PastFrames = 0;
    stream.FutureFrames = 0;
    stream.pInputSurface = inputView.Get();

    hr = videoContext->VideoProcessorBlt(videoProcessor_.Get(), outputView.Get(), 0, 1, &stream);
    return SUCCEEDED(hr);
}

void D3D11VideoProcessor::Release() {
    videoProcessor_.Reset();
    enumerator_.Reset();
    videoDevice_.Reset();
    device_.Reset();
    isInitialized_ = false;
    isSupported_ = false;
    currentSrcW_ = 0;
    currentSrcH_ = 0;
}

} // namespace km::media
