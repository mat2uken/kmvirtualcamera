#include "d3d11_preview.h"
#include <algorithm>

namespace km::ui {

D3D11Preview::D3D11Preview() = default;

D3D11Preview::~D3D11Preview() {
    std::lock_guard<std::mutex> lock(renderMutex_);
    renderTargetView_.Reset();
    videoTexture_.Reset();
    textureView_.Reset();
    swapChain_.Reset();
    context_.Reset();
    device_.Reset();
}

bool D3D11Preview::Initialize(HWND hWnd, int width, int height) {
    std::lock_guard<std::mutex> lock(renderMutex_);
    hWnd_ = hWnd;
    width_ = width > 0 ? width : 1280;
    height_ = height > 0 ? height : 720;
    bgraStaging_.resize(1280 * 720, 0xFF000000);

    DXGI_SWAP_CHAIN_DESC scd{};
    scd.BufferCount = 1;
    scd.BufferDesc.Width = static_cast<UINT>(width_);
    scd.BufferDesc.Height = static_cast<UINT>(height_);
    scd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    scd.BufferDesc.RefreshRate.Numerator = 60;
    scd.BufferDesc.RefreshRate.Denominator = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = hWnd_;
    scd.SampleDesc.Count = 1;
    scd.SampleDesc.Quality = 0;
    scd.Windowed = TRUE;
    scd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL featureLevel;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        0,
        featureLevels,
        2,
        D3D11_SDK_VERSION,
        &scd,
        &swapChain_,
        &device_,
        &featureLevel,
        &context_
    );

    if (FAILED(hr)) {
        // Fallback to WARP software renderer
        hr = D3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_WARP,
            nullptr,
            0,
            featureLevels,
            2,
            D3D11_SDK_VERSION,
            &scd,
            &swapChain_,
            &device_,
            &featureLevel,
            &context_
        );
    }
    if (FAILED(hr)) return false;

    // Create render target view
    Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
    hr = swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (FAILED(hr)) return false;

    hr = device_->CreateRenderTargetView(backBuffer.Get(), nullptr, &renderTargetView_);
    if (FAILED(hr)) return false;

    // Create dynamic staging texture
    D3D11_TEXTURE2D_DESC td{};
    td.Width = 1280;
    td.Height = 720;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    device_->CreateTexture2D(&td, nullptr, &videoTexture_);

    return true;
}

void D3D11Preview::Resize(int width, int height) {
    if (width <= 0 || height <= 0 || !swapChain_) return;
    std::lock_guard<std::mutex> lock(renderMutex_);

    renderTargetView_.Reset();
    swapChain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);

    Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
    if (SUCCEEDED(swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuffer)))) {
        device_->CreateRenderTargetView(backBuffer.Get(), nullptr, &renderTargetView_);
    }
    width_ = width;
    height_ = height;
}

void D3D11Preview::RenderNv12Frame(std::span<const uint8_t> nv12Data, int width, int height) {
    if (nv12Data.empty() || width <= 0 || height <= 0 || !hWnd_) return;
    std::lock_guard<std::mutex> lock(renderMutex_);

    if (bgraStaging_.size() != static_cast<size_t>(width * height)) {
        bgraStaging_.resize(width * height);
    }

    // Convert NV12 to BGRA for preview presentation
    const uint8_t* yPlane = nv12Data.data();
    const uint8_t* uvPlane = nv12Data.data() + (width * height);

    for (int y = 0; y < height; ++y) {
        const uint8_t* yRow = yPlane + (y * width);
        const uint8_t* uvRow = uvPlane + ((y / 2) * width);
        uint32_t* dstRow = bgraStaging_.data() + (y * width);

        for (int x = 0; x < width; x += 2) {
            int uVal = static_cast<int>(uvRow[x]) - 128;
            int vVal = static_cast<int>(uvRow[x + 1]) - 128;

            int rCoeff = 409 * vVal;
            int gCoeff = -100 * uVal - 208 * vVal;
            int bCoeff = 516 * uVal;

            int c0 = (static_cast<int>(yRow[x]) - 16) * 298 + 128;
            int r0 = std::clamp((c0 + rCoeff) >> 8, 0, 255);
            int g0 = std::clamp((c0 + gCoeff) >> 8, 0, 255);
            int b0 = std::clamp((c0 + bCoeff) >> 8, 0, 255);
            dstRow[x] = 0xFF000000 | (r0 << 16) | (g0 << 8) | b0;

            int c1 = (static_cast<int>(yRow[x + 1]) - 16) * 298 + 128;
            int r1 = std::clamp((c1 + rCoeff) >> 8, 0, 255);
            int g1 = std::clamp((c1 + gCoeff) >> 8, 0, 255);
            int b1 = std::clamp((c1 + bCoeff) >> 8, 0, 255);
            dstRow[x + 1] = 0xFF000000 | (r1 << 16) | (g1 << 8) | b1;
        }
    }

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    HDC hdc = GetDC(hWnd_);
    if (hdc) {
        RECT rc{};
        GetClientRect(hWnd_, &rc);
        int clientW = rc.right - rc.left;
        int clientH = rc.bottom - rc.top;

        SetStretchBltMode(hdc, HALFTONE);
        SetBrushOrgEx(hdc, 0, 0, nullptr);

        StretchDIBits(
            hdc,
            0, 0, clientW, clientH,
            0, 0, width, height,
            bgraStaging_.data(),
            &bmi,
            DIB_RGB_COLORS,
            SRCCOPY
        );

        ReleaseDC(hWnd_, hdc);
    }
}

} // namespace km::ui
