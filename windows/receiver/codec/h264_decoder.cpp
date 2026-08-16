#include "h264_decoder.h"
#include <wmcodecdsp.h>
#include <codecapi.h>
#include <algorithm>

namespace km::codec {

// {62CE7E72-4C71-4D20-B15D-452831A87D9D}
static const GUID CLSID_CMSH264DecoderMFT_Local = { 0x62CE7E72, 0x4C71, 0x4D20, { 0xB1, 0x5D, 0x45, 0x28, 0x31, 0xA8, 0x7D, 0x9D } };

H264Decoder::H264Decoder() = default;

H264Decoder::~H264Decoder() {
    Shutdown();
}

bool H264Decoder::Initialize(int width, int height) {
    std::lock_guard<std::mutex> lock(mutex_);
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

    // Enable Low Latency mode on Decoder MFT
    Microsoft::WRL::ComPtr<ICodecAPI> codecApi;
    if (SUCCEEDED(decoderMft_.As(&codecApi)) && codecApi) {
        VARIANT varLowLatency{};
        varLowLatency.vt = VT_BOOL;
        varLowLatency.boolVal = VARIANT_TRUE;
        codecApi->SetValue(&CODECAPI_AVLowLatencyMode, &varLowLatency);
    }

    Microsoft::WRL::ComPtr<IMFAttributes> attributes;
    if (SUCCEEDED(decoderMft_->GetAttributes(&attributes)) && attributes) {
        attributes->SetUINT32(CODECAPI_AVDecVideoThumbnailGenerationMode, 0);
        attributes->SetUINT32(CODECAPI_AVLowLatencyMode, 1);
        attributes->SetUINT32(MF_LOW_LATENCY, 1);
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
    std::lock_guard<std::mutex> lock(mutex_);
    if (!isInitialized_) return;

    if (decoderMft_) {
        decoderMft_->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
        decoderMft_->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING, 0);
        decoderMft_.Reset();
    }
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
    HRESULT hr = pSample->ConvertToContiguousBuffer(&mediaBuffer);
    if (FAILED(hr) || !mediaBuffer) return false;

    int codedW = actualWidth_;
    int codedH = actualHeight_;
    int dispW = displayWidth_ > 0 ? displayWidth_ : codedW;
    int dispH = displayHeight_ > 0 ? displayHeight_ : codedH;

    size_t expectedNv12Size = static_cast<size_t>(dispW * dispH * 3 / 2);
    if (outNv12.size() != expectedNv12Size) {
        outNv12.resize(expectedNv12Size);
    }
    // Initialize to clean neutral studio black (Y=16, UV=128)
    std::fill_n(outNv12.begin(), dispW * dispH, static_cast<uint8_t>(16));
    std::fill_n(outNv12.begin() + (dispW * dispH), dispW * dispH / 2, static_cast<uint8_t>(128));

    BYTE* pSrc = nullptr;
    DWORD currentLen = 0;
    hr = mediaBuffer->Lock(&pSrc, nullptr, &currentLen);
    if (SUCCEEDED(hr) && pSrc) {
        if (dispW == codedW && dispH == codedH) {
            size_t copyBytes = (std::min)(static_cast<size_t>(currentLen), expectedNv12Size);
            memcpy(outNv12.data(), pSrc, copyBytes);
        } else {
            // Crop out bottom/side macroblock padding:
            // Y Plane: copy dispH rows of dispW bytes from row stride codedW
            for (int y = 0; y < dispH; ++y) {
                memcpy(outNv12.data() + (y * dispW), pSrc + (y * codedW), dispW);
            }
            // UV Plane: starts at offset (codedW * codedH) in source buffer
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

bool H264Decoder::DecodeAccessUnit(
    const uint8_t* h264Data,
    size_t size,
    int64_t timestampUs,
    std::vector<uint8_t>& outNv12,
    int& outWidth,
    int& outHeight
) {
    if (!h264Data || size == 0) return false;

    std::lock_guard<std::mutex> lock(mutex_);
    if (!isInitialized_ && !Initialize(targetWidth_, targetHeight_)) {
        return false;
    }

    // 1. Create Input Sample Buffer
    Microsoft::WRL::ComPtr<IMFMediaBuffer> inBuffer;
    HRESULT hr = MFCreateMemoryBuffer(static_cast<DWORD>(size), &inBuffer);
    if (FAILED(hr)) return false;

    BYTE* pDst = nullptr;
    hr = inBuffer->Lock(&pDst, nullptr, nullptr);
    if (FAILED(hr)) return false;

    memcpy(pDst, h264Data, size);
    inBuffer->Unlock();
    inBuffer->SetCurrentLength(static_cast<DWORD>(size));

    Microsoft::WRL::ComPtr<IMFSample> inSample;
    hr = MFCreateSample(&inSample);
    if (FAILED(hr)) return false;

    inSample->AddBuffer(inBuffer.Get());
    inSample->SetSampleTime(timestampUs * 10);
    inSample->SetSampleDuration(333333);

    // 2. Feed Sample to MFT
    hr = decoderMft_->ProcessInput(0, inSample.Get(), 0);
    if (FAILED(hr) && hr != MF_E_NOTACCEPTING) {
        return false;
    }

    // 3. Process Output from MFT in a drain loop
    bool gotFrame = false;

    for (int iter = 0; iter < 4; ++iter) {
        MFT_OUTPUT_STREAM_INFO streamInfo{};
        hr = decoderMft_->GetOutputStreamInfo(0, &streamInfo);
        if (FAILED(hr)) break;

        MFT_OUTPUT_DATA_BUFFER outputBuffer{};
        outputBuffer.dwStreamID = 0;

        Microsoft::WRL::ComPtr<IMFSample> outSample;
        Microsoft::WRL::ComPtr<IMFMediaBuffer> outBuffer;

        if (!(streamInfo.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES | MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES))) {
            DWORD cbSize = streamInfo.cbSize > 0 ? streamInfo.cbSize : (actualWidth_ * actualHeight_ * 3 / 2);
            hr = MFCreateMemoryBuffer(cbSize, &outBuffer);
            if (FAILED(hr)) break;

            hr = MFCreateSample(&outSample);
            if (FAILED(hr)) break;

            outSample->AddBuffer(outBuffer.Get());
            outputBuffer.pSample = outSample.Get();
        }

        DWORD dwStatus = 0;
        hr = decoderMft_->ProcessOutput(0, 1, &outputBuffer, &dwStatus);

        if (outputBuffer.pEvents) {
            outputBuffer.pEvents->Release();
        }

        if (hr == MF_E_TRANSFORM_STREAM_CHANGE) {
            // MFT detected actual stream resolution from SPS
            Microsoft::WRL::ComPtr<IMFMediaType> availType;
            if (SUCCEEDED(decoderMft_->GetOutputAvailableType(0, 0, &availType))) {
                UINT32 w = 0, h = 0;
                if (SUCCEEDED(MFGetAttributeSize(availType.Get(), MF_MT_FRAME_SIZE, &w, &h)) && w > 0 && h > 0) {
                    actualWidth_ = static_cast<int>(w);
                    actualHeight_ = static_cast<int>(h);
                    ConfigureOutputType(actualWidth_, actualHeight_);
                }
            }
            continue; // Continue loop to drain the frame for this new format
        }

        if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT) {
            break;
        }

        if (SUCCEEDED(hr) && outputBuffer.pSample) {
            if (ExtractSampleNv12(outputBuffer.pSample, outNv12, outWidth, outHeight)) {
                gotFrame = true;
                ++sampleIndex_;
                break;
            }
        }
    }

    return gotFrame;
}

} // namespace km::codec
