#include "h264_decoder.h"
#include <wmcodecdsp.h>
#include <codecapi.h>
#include <algorithm>
#include <thread>

namespace km::codec {

// {62CE7E72-4C71-4D20-B15D-452831A87D9D}
static const GUID CLSID_CMSH264DecoderMFT_Local = { 0x62CE7E72, 0x4C71, 0x4D20, { 0xB1, 0x5D, 0x45, 0x28, 0x31, 0xA8, 0x7D, 0x9D } };

H264Decoder::H264Decoder() = default;

H264Decoder::~H264Decoder() {
    Shutdown();
}

bool H264Decoder::Initialize(int width, int height, ID3D11Device* pD3DDevice) {
    if (isInitialized_) return true;

    targetWidth_ = width > 0 ? width : 1280;
    targetHeight_ = height > 0 ? height : 720;
    actualWidth_ = targetWidth_;
    actualHeight_ = targetHeight_;

    HRESULT hr = CoCreateInstance(
        CLSID_CMSH264DecoderMFT_Local,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_IMFTransform,
        (void**)&decoderMft_
    );

    if (FAILED(hr) || !decoderMft_) {
        std::cerr << "[H264Decoder] Failed to instantiate CLSID_CMSH264DecoderMFT: hr=0x" << std::hex << hr << std::dec << std::endl;
        return false;
    }

    // Enable Low Latency and Multi-Slice Parallel Decoding mode on Decoder MFT
    unsigned int numThreads = (std::max)(2u, std::thread::hardware_concurrency());
    Microsoft::WRL::ComPtr<ICodecAPI> codecApi;
    if (SUCCEEDED(decoderMft_.As(&codecApi)) && codecApi) {
        VARIANT varLowLatency{};
        varLowLatency.vt = VT_BOOL;
        varLowLatency.boolVal = VARIANT_TRUE;
        codecApi->SetValue(&CODECAPI_AVLowLatencyMode, &varLowLatency);

        VARIANT varThreads{};
        varThreads.vt = VT_UI4;
        varThreads.ulVal = numThreads;
        codecApi->SetValue(&CODECAPI_AVDecNumWorkerThreads, &varThreads);
    }

    Microsoft::WRL::ComPtr<IMFAttributes> attributes;
    if (SUCCEEDED(decoderMft_->GetAttributes(&attributes)) && attributes) {
        attributes->SetUINT32(CODECAPI_AVDecNumWorkerThreads, numThreads);
        attributes->SetUINT32(CODECAPI_AVDecVideoThumbnailGenerationMode, 0);
        attributes->SetUINT32(CODECAPI_AVLowLatencyMode, 1);
        attributes->SetUINT32(MF_LOW_LATENCY, 1);
        attributes->SetUINT32(MF_SA_MINIMUM_OUTPUT_SAMPLE_COUNT, 1);
        attributes->SetUINT32(MF_MT_REALTIME_CONTENT, 1);
        if (pD3DDevice) {
            attributes->SetUINT32(MF_SA_D3D11_AWARE, 1);
        }
    }

    // Attach D3D11 DXGI Device Manager if device provided for hardware DXVA decoding
    if (pD3DDevice) {
        d3dDevice_ = pD3DDevice;
        UINT resetToken = 0;
        hr = MFCreateDXGIDeviceManager(&resetToken, &dxgiDeviceManager_);
        if (SUCCEEDED(hr) && dxgiDeviceManager_) {
            dxgiResetToken_ = resetToken;
            hr = dxgiDeviceManager_->ResetDevice(d3dDevice_.Get(), dxgiResetToken_);
            if (SUCCEEDED(hr)) {
                hr = decoderMft_->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER, reinterpret_cast<ULONG_PTR>(dxgiDeviceManager_.Get()));
                if (SUCCEEDED(hr)) {
                    isHardwareAccelerated_ = true;
                    std::cout << "[H264Decoder] Hardware Acceleration (D3D11 / DXVA) ENABLED for MFT H.264 Decoder." << std::endl;
                }
            }
        }
    }

    // Configure Input MediaType (H.264)
    Microsoft::WRL::ComPtr<IMFMediaType> inputType;
    hr = CreateInputType(targetWidth_, targetHeight_, &inputType);
    if (FAILED(hr)) {
        std::cerr << "[H264Decoder] Failed to create input media type: hr=0x" << std::hex << hr << std::dec << std::endl;
        decoderMft_.Reset();
        return false;
    }

    hr = decoderMft_->SetInputType(0, inputType.Get(), 0);
    if (FAILED(hr)) {
        std::cerr << "[H264Decoder] Failed to set input media type: hr=0x" << std::hex << hr << std::dec << std::endl;
        decoderMft_.Reset();
        return false;
    }

    // Configure Output MediaType (NV12)
    hr = ConfigureOutputType(targetWidth_, targetHeight_);
    if (FAILED(hr)) {
        std::cerr << "[H264Decoder] Failed to configure output media type: hr=0x" << std::hex << hr << std::dec << std::endl;
        decoderMft_.Reset();
        return false;
    }

    // Notify Begin Streaming
    decoderMft_->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
    decoderMft_->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);

    isInitialized_ = true;
    sampleIndex_ = 0;
    return true;
}

void H264Decoder::Shutdown() {
    if (!isInitialized_) return;

    if (decoderMft_) {
        decoderMft_->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
        decoderMft_->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING, 0);
        decoderMft_.Reset();
    }
    dxgiDeviceManager_.Reset();
    d3dDevice_.Reset();
    isHardwareAccelerated_ = false;
    inSample_.Reset();
    inBuffer_.Reset();
    inBufferCapacity_ = 0;
    outSample_.Reset();
    outBuffer_.Reset();
    outBufferCapacity_ = 0;
    isInitialized_ = false;
}

HRESULT H264Decoder::CreateInputType(int width, int height, IMFMediaType** ppType) {
    if (!ppType) return E_POINTER;
    *ppType = nullptr;

    Microsoft::WRL::ComPtr<IMFMediaType> type;
    HRESULT hr = MFCreateMediaType(&type);
    if (FAILED(hr)) return hr;

    hr = type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    if (FAILED(hr)) return hr;

    hr = type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    if (FAILED(hr)) return hr;

    hr = MFSetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, width, height);
    if (FAILED(hr)) return hr;

    hr = MFSetAttributeRatio(type.Get(), MF_MT_FRAME_RATE, 30, 1);
    if (FAILED(hr)) return hr;

    hr = MFSetAttributeRatio(type.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    if (FAILED(hr)) return hr;

    hr = type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    if (FAILED(hr)) return hr;

    type->SetUINT32(MF_LOW_LATENCY, 1);
    type->SetUINT32(MF_MT_REALTIME_CONTENT, 1);

    *ppType = type.Detach();
    return S_OK;
}

HRESULT H264Decoder::ConfigureOutputType(int width, int height) {
    DWORD typeIndex = 0;
    Microsoft::WRL::ComPtr<IMFMediaType> availableType;

    while (SUCCEEDED(decoderMft_->GetOutputAvailableType(0, typeIndex++, &availableType))) {
        GUID subtype = GUID_NULL;
        if (SUCCEEDED(availableType->GetGUID(MF_MT_SUBTYPE, &subtype))) {
            if (subtype == MFVideoFormat_NV12) {
                Microsoft::WRL::ComPtr<IMFMediaType> selectedType;
                HRESULT hr = MFCreateMediaType(&selectedType);
                if (FAILED(hr)) return hr;

                availableType->CopyAllItems(selectedType.Get());
                MFSetAttributeSize(selectedType.Get(), MF_MT_FRAME_SIZE, width, height);
                MFSetAttributeRatio(selectedType.Get(), MF_MT_FRAME_RATE, 30, 1);
                MFSetAttributeRatio(selectedType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
                selectedType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);

                hr = decoderMft_->SetOutputType(0, selectedType.Get(), 0);
                if (SUCCEEDED(hr)) {
                    actualWidth_ = width;
                    actualHeight_ = height;

                    // Query display aperture to detect and eliminate macroblock crop padding (e.g. 192 -> 180, 272 -> 270, 1088 -> 1080)
                    MFVideoArea aperture{};
                    UINT32 blobSize = sizeof(MFVideoArea);
                    if (SUCCEEDED(selectedType->GetBlob(MF_MT_MINIMUM_DISPLAY_APERTURE, (UINT8*)&aperture, sizeof(MFVideoArea), &blobSize)) && aperture.Area.cx > 0 && aperture.Area.cy > 0) {
                        displayWidth_ = static_cast<int>(aperture.Area.cx);
                        displayHeight_ = static_cast<int>(aperture.Area.cy);
                    } else if (SUCCEEDED(availableType->GetBlob(MF_MT_MINIMUM_DISPLAY_APERTURE, (UINT8*)&aperture, sizeof(MFVideoArea), &blobSize)) && aperture.Area.cx > 0 && aperture.Area.cy > 0) {
                        displayWidth_ = static_cast<int>(aperture.Area.cx);
                        displayHeight_ = static_cast<int>(aperture.Area.cy);
                    } else {
                        // Standard macroblock-padded 16:9 resolutions
                        if (height == 1088 && width == 1920) displayHeight_ = 1080;
                        else if (height == 272 && width == 480) displayHeight_ = 270;
                        else if (height == 192 && width == 320) displayHeight_ = 180;
                        else displayHeight_ = height;
                        displayWidth_ = width;
                    }

                    // Enforce even dimensions for 4:2:0 subsampling
                    displayWidth_ = (displayWidth_ / 2) * 2;
                    displayHeight_ = (displayHeight_ / 2) * 2;

                    std::cout << "[H264Decoder] Configured format: Coded=" << actualWidth_ << "x" << actualHeight_
                              << ", Visible=" << displayWidth_ << "x" << displayHeight_ << std::endl;
                    return S_OK;
                }
            }
        }
        availableType.Reset();
    }

    return E_FAIL;
}

bool H264Decoder::ExtractSampleNv12(IMFSample* pSample, std::vector<uint8_t>& outNv12, int& outW, int& outH) {
    if (!pSample) return false;

    Microsoft::WRL::ComPtr<IMFMediaBuffer> mediaBuffer;
    HRESULT hr = pSample->GetBufferByIndex(0, &mediaBuffer);
    if (FAILED(hr) || !mediaBuffer) {
        hr = pSample->ConvertToContiguousBuffer(&mediaBuffer);
        if (FAILED(hr) || !mediaBuffer) return false;
    }

    int codedW = actualWidth_;
    int codedH = actualHeight_;
    int dispW = displayWidth_ > 0 ? displayWidth_ : codedW;
    int dispH = displayHeight_ > 0 ? displayHeight_ : codedH;

    size_t expectedNv12Size = static_cast<size_t>(dispW * dispH * 3 / 2);
    if (outNv12.size() != expectedNv12Size) {
        outNv12.resize(expectedNv12Size);
    }

    // 1. Try 2D Buffer Lock (Supports IMFDXGIBuffer / Direct3D hardware surfaces)
    Microsoft::WRL::ComPtr<IMF2DBuffer> buffer2D;
    if (SUCCEEDED(mediaBuffer.As(&buffer2D)) && buffer2D) {
        BYTE* pScanline = nullptr;
        LONG pitch = 0;
        hr = buffer2D->Lock2D(&pScanline, &pitch);
        if (SUCCEEDED(hr) && pScanline) {
            LONG absPitch = std::abs(pitch);
            // Y Plane
            for (int y = 0; y < dispH; ++y) {
                memcpy(outNv12.data() + (y * dispW), pScanline + (y * absPitch), dispW);
            }
            // UV Plane: starts at (codedH * absPitch)
            const BYTE* pUvScanline = pScanline + (codedH * absPitch);
            uint8_t* pUvDst = outNv12.data() + (dispW * dispH);
            for (int y = 0; y < dispH / 2; ++y) {
                memcpy(pUvDst + (y * dispW), pUvScanline + (y * absPitch), dispW);
            }
            buffer2D->Unlock2D();
            outW = dispW;
            outH = dispH;
            return true;
        }
    }

    // 2. Standard 1D Buffer Lock (Software MFT buffers)
    BYTE* pSrc = nullptr;
    DWORD currentLen = 0;
    hr = mediaBuffer->Lock(&pSrc, nullptr, &currentLen);
    if (SUCCEEDED(hr) && pSrc) {
        if (dispW == codedW && dispH == codedH) {
            // Fast path: zero-copy direct memcpy
            size_t copyBytes = (std::min)(static_cast<size_t>(currentLen), expectedNv12Size);
            memcpy(outNv12.data(), pSrc, copyBytes);
        } else {
            // Crop path: copy visible region
            for (int y = 0; y < dispH; ++y) {
                memcpy(outNv12.data() + (y * dispW), pSrc + (y * codedW), dispW);
            }
            const BYTE* pUvSrc = pSrc + (codedW * codedH);
            uint8_t* pUvDst = outNv12.data() + (dispW * dispH);
            for (int y = 0; y < dispH / 2; ++y) {
                memcpy(pUvDst + (y * dispW), pUvSrc + (y * codedW), dispW);
            }
        }
        mediaBuffer->Unlock();
        outW = dispW;
        outH = dispH;
        return true;
    }

    return false;
}

bool H264Decoder::DecodeAccessUnitEx(
    const uint8_t* h264Data,
    size_t size,
    int64_t timestampUs,
    GpuDecodedFrame& outGpuFrame,
    std::vector<uint8_t>& outCpuNv12,
    bool& outIsGpuDirect
) {
    outIsGpuDirect = false;
    if (!h264Data || size == 0) return false;

    if (!isInitialized_ && !Initialize(targetWidth_, targetHeight_, d3dDevice_.Get())) {
        return false;
    }

    // 1. Reusable Input Sample Buffer (Zero heap allocation in steady state)
    if (!inBuffer_ || inBufferCapacity_ < size) {
        DWORD req = static_cast<DWORD>(size * 2);
        DWORD minCap = 512u * 1024u;
        inBufferCapacity_ = (req > minCap) ? req : minCap;
        inBuffer_.Reset();
        inSample_.Reset();
        HRESULT hr = MFCreateMemoryBuffer(inBufferCapacity_, &inBuffer_);
        if (FAILED(hr)) return false;
        hr = MFCreateSample(&inSample_);
        if (FAILED(hr)) return false;
        inSample_->AddBuffer(inBuffer_.Get());
    }

    BYTE* pDst = nullptr;
    HRESULT hr = inBuffer_->Lock(&pDst, nullptr, nullptr);
    if (FAILED(hr)) return false;

    memcpy(pDst, h264Data, size);
    inBuffer_->Unlock();
    inBuffer_->SetCurrentLength(static_cast<DWORD>(size));

    inSample_->SetSampleTime(timestampUs * 10);
    inSample_->SetSampleDuration(166666); // 60fps base interval

    // 2. Helper lambda to drain all ready output samples from MFT
    auto DrainOutput = [&]() -> bool {
        bool gotAny = false;
        for (int iter = 0; iter < 8; ++iter) {
            MFT_OUTPUT_STREAM_INFO streamInfo{};
            HRESULT ohr = decoderMft_->GetOutputStreamInfo(0, &streamInfo);
            if (FAILED(ohr)) break;

            MFT_OUTPUT_DATA_BUFFER outputBuffer{};
            outputBuffer.dwStreamID = 0;

            bool mftProvidesSamples = (streamInfo.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES | MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES)) != 0;
            if (!mftProvidesSamples) {
                DWORD cbSize = streamInfo.cbSize > 0 ? streamInfo.cbSize : static_cast<DWORD>(actualWidth_ * actualHeight_ * 3 / 2);
                DWORD minOutCap = 1920u * 1088u * 2u;
                DWORD targetCap = (cbSize > minOutCap) ? cbSize : minOutCap;
                if (!outBuffer_ || outBufferCapacity_ < targetCap) {
                    outBufferCapacity_ = targetCap;
                    outBuffer_.Reset();
                    outSample_.Reset();
                    ohr = MFCreateMemoryBuffer(outBufferCapacity_, &outBuffer_);
                    if (FAILED(ohr)) break;
                    ohr = MFCreateSample(&outSample_);
                    if (FAILED(ohr)) break;
                    outSample_->AddBuffer(outBuffer_.Get());
                }
                outBuffer_->SetCurrentLength(0);
                outputBuffer.pSample = outSample_.Get();
            }

            DWORD dwStatus = 0;
            ohr = decoderMft_->ProcessOutput(0, 1, &outputBuffer, &dwStatus);

            if (outputBuffer.pEvents) {
                outputBuffer.pEvents->Release();
            }

            if (ohr == MF_E_TRANSFORM_STREAM_CHANGE) {
                Microsoft::WRL::ComPtr<IMFMediaType> availType;
                if (SUCCEEDED(decoderMft_->GetOutputAvailableType(0, 0, &availType))) {
                    UINT32 w = 0, h = 0;
                    if (SUCCEEDED(MFGetAttributeSize(availType.Get(), MF_MT_FRAME_SIZE, &w, &h)) && w > 0 && h > 0) {
                        actualWidth_ = static_cast<int>(w);
                        actualHeight_ = static_cast<int>(h);
                        ConfigureOutputType(actualWidth_, actualHeight_);
                    }
                }
                continue;
            }

            if (ohr == MF_E_TRANSFORM_NEED_MORE_INPUT) {
                break;
            }

            if (SUCCEEDED(ohr) && outputBuffer.pSample) {
                Microsoft::WRL::ComPtr<IMFMediaBuffer> buf;
                if (SUCCEEDED(outputBuffer.pSample->GetBufferByIndex(0, &buf)) && buf) {
                    Microsoft::WRL::ComPtr<IMFDXGIBuffer> dxgiBuf;
                    if (SUCCEEDED(buf.As(&dxgiBuf)) && dxgiBuf) {
                        Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
                        UINT subIndex = 0;
                        if (SUCCEEDED(dxgiBuf->GetResource(IID_PPV_ARGS(&tex))) && tex && SUCCEEDED(dxgiBuf->GetSubresourceIndex(&subIndex))) {
                            outGpuFrame.texture = tex;
                            outGpuFrame.subresourceIndex = subIndex;
                            outGpuFrame.codedWidth = actualWidth_;
                            outGpuFrame.codedHeight = actualHeight_;
                            outGpuFrame.displayWidth = displayWidth_ > 0 ? displayWidth_ : actualWidth_;
                            outGpuFrame.displayHeight = displayHeight_ > 0 ? displayHeight_ : actualHeight_;
                            outIsGpuDirect = true;
                        }
                    }
                }

                int decW = 0, decH = 0;
                if (ExtractSampleNv12(outputBuffer.pSample, outCpuNv12, decW, decH)) {
                    gotAny = true;
                    ++sampleIndex_;
                } else if (outIsGpuDirect) {
                    gotAny = true;
                    ++sampleIndex_;
                }
            }
        }
        return gotAny;
    };

    // 3. Feed Sample to MFT with automatic drain retry
    hr = decoderMft_->ProcessInput(0, inSample_.Get(), 0);
    if (hr == MF_E_NOTACCEPTING) {
        DrainOutput();
        hr = decoderMft_->ProcessInput(0, inSample_.Get(), 0);
    }
    if (FAILED(hr)) {
        return false;
    }

    // 4. Drain output after feeding
    return DrainOutput();
}

bool H264Decoder::DecodeAccessUnit(
    const uint8_t* h264Data,
    size_t size,
    int64_t timestampUs,
    std::vector<uint8_t>& outNv12,
    int& outWidth,
    int& outHeight
) {
    GpuDecodedFrame gpuFrame{};
    bool isGpuDirect = false;
    if (!DecodeAccessUnitEx(h264Data, size, timestampUs, gpuFrame, outNv12, isGpuDirect)) {
        return false;
    }
    outWidth = displayWidth_ > 0 ? displayWidth_ : actualWidth_;
    outHeight = displayHeight_ > 0 ? displayHeight_ : actualHeight_;
    return true;
}

} // namespace km::codec
