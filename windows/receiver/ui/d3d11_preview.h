#pragma once

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <cstdint>
#include <vector>
#include <span>
#include <mutex>

namespace km::ui {

class D3D11Preview {
public:
    D3D11Preview();
    ~D3D11Preview();

    bool Initialize(HWND hWnd, int width = 1280, int height = 720);
    void Resize(int width, int height);
    void RenderNv12Frame(std::span<const uint8_t> nv12Data, int width = 1280, int height = 720);

private:
    HWND hWnd_{nullptr};
    int width_{1280};
    int height_{720};
    std::mutex renderMutex_;

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<IDXGISwapChain> swapChain_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> renderTargetView_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> videoTexture_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> textureView_;

    std::vector<uint32_t> bgraStaging_;
};

} // namespace km::ui
