#pragma once

#include <windows.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <cstdint>
#include <atomic>
#include <iostream>
#include "frame_pipe_protocol.h"
#include "shared_memory_frame.h"

namespace km::dxgi {

inline constexpr wchar_t kDxgiSharedTexNameSlot0[] = L"Local\\KMVirtualCamera_DXGI_Slot0";
inline constexpr wchar_t kDxgiSharedTexNameSlot1[] = L"Local\\KMVirtualCamera_DXGI_Slot1";

class DxgiTexturePublisher {
public:
    DxgiTexturePublisher() = default;
    ~DxgiTexturePublisher() { Close(); }

    bool Initialize(ID3D11Device* pDevice, UINT width = protocol::kWidth, UINT height = protocol::kHeight) {
        if (!pDevice) return false;
        Close();

        device_ = pDevice;
        width_ = width;
        height_ = height;

        Microsoft::WRL::ComPtr<ID3D11Device1> device1;
        pDevice->QueryInterface(IID_PPV_ARGS(&device1));

        for (int slot = 0; slot < 2; ++slot) {
            D3D11_TEXTURE2D_DESC desc = {};
            desc.Width = width_;
            desc.Height = height_;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_NV12;
            desc.SampleDesc.Count = 1;
            desc.SampleDesc.Quality = 0;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
            desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;

            HRESULT hr = pDevice->CreateTexture2D(&desc, nullptr, &textures_[slot]);
            if (FAILED(hr)) {
                // Fallback without KeyedMutex if not supported
                desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
                hr = pDevice->CreateTexture2D(&desc, nullptr, &textures_[slot]);
            }
            if (FAILED(hr)) {
                // Legacy shared handle fallback
                desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
                hr = pDevice->CreateTexture2D(&desc, nullptr, &textures_[slot]);
            }

            if (FAILED(hr) || !textures_[slot]) {
                std::cerr << "[DxgiTexturePublisher] Failed to create shared texture for slot " << slot << ": hr=0x" << std::hex << hr << std::dec << std::endl;
                Close();
                return false;
            }

            Microsoft::WRL::ComPtr<IDXGIResource1> res1;
            if (SUCCEEDED(textures_[slot].As(&res1)) && res1) {
                LPCWSTR name = (slot == 0) ? kDxgiSharedTexNameSlot0 : kDxgiSharedTexNameSlot1;
                HANDLE hShared = nullptr;
                hr = res1->CreateSharedHandle(
                    nullptr,
                    DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
                    name,
                    &hShared
                );
                if (SUCCEEDED(hr) && hShared) {
                    sharedHandles_[slot] = hShared;
                }
            } else {
                Microsoft::WRL::ComPtr<IDXGIResource> res;
                if (SUCCEEDED(textures_[slot].As(&res)) && res) {
                    HANDLE hLegacy = nullptr;
                    if (SUCCEEDED(res->GetSharedHandle(&hLegacy))) {
                        sharedHandles_[slot] = hLegacy;
                    }
                }
            }

            textures_[slot].As(&keyedMutexes_[slot]);
        }

        isInitialized_ = true;
        return true;
    }

    bool PublishNv12Frame(
        ID3D11DeviceContext* pContext,
        const uint8_t* pNv12Data,
        UINT width,
        UINT height,
        int64_t tsUs
    ) {
        if (!isInitialized_ || !pContext || !pNv12Data) return false;

        uint32_t targetSlot = 1 - currentSlot_.load(std::memory_order_relaxed);
        auto& tex = textures_[targetSlot];
        if (!tex) return false;

        if (keyedMutexes_[targetSlot]) {
            keyedMutexes_[targetSlot]->AcquireSync(0, 5);
        }

        // Subresource 0 in NV12 contains whole Y + UV contiguous buffer
        pContext->UpdateSubresource(tex.Get(), 0, nullptr, pNv12Data, width, width * height * 3 / 2);

        if (keyedMutexes_[targetSlot]) {
            keyedMutexes_[targetSlot]->ReleaseSync(1);
        }

        currentSlot_.store(targetSlot, std::memory_order_release);
        return true;
    }

    bool PublishGpuTextureDirect(
        ID3D11DeviceContext* pContext,
        ID3D11Texture2D* pSrcTexture,
        UINT srcSubresource,
        UINT srcWidth,
        UINT srcHeight,
        int64_t tsUs
    ) {
        if (!isInitialized_ || !pContext || !pSrcTexture) return false;

        uint32_t targetSlot = 1 - currentSlot_.load(std::memory_order_relaxed);
        auto& tex = textures_[targetSlot];
        if (!tex) return false;

        if (keyedMutexes_[targetSlot]) {
            keyedMutexes_[targetSlot]->AcquireSync(0, 5);
        }

        if (srcWidth == width_ && srcHeight == height_) {
            // Direct 1:1 GPU DMA subresource copy (<0.02ms)
            pContext->CopySubresourceRegion(tex.Get(), 0, 0, 0, 0, pSrcTexture, srcSubresource, nullptr);
        } else {
            // Box-clipped copy or viewport
            D3D11_BOX box{};
            box.left = 0;
            box.top = 0;
            box.front = 0;
            box.right = (std::min)(srcWidth, width_);
            box.bottom = (std::min)(srcHeight, height_);
            box.back = 1;
            pContext->CopySubresourceRegion(tex.Get(), 0, 0, 0, 0, pSrcTexture, srcSubresource, &box);
        }

        if (keyedMutexes_[targetSlot]) {
            keyedMutexes_[targetSlot]->ReleaseSync(1);
        }

        currentSlot_.store(targetSlot, std::memory_order_release);
        return true;
    }

    void Close() {
        for (int i = 0; i < 2; ++i) {
            keyedMutexes_[i].Reset();
            textures_[i].Reset();
            if (sharedHandles_[i]) {
                CloseHandle(sharedHandles_[i]);
                sharedHandles_[i] = nullptr;
            }
        }
        device_.Reset();
        isInitialized_ = false;
    }

    bool IsInitialized() const { return isInitialized_; }
    uint32_t GetActiveSlot() const { return currentSlot_.load(std::memory_order_acquire); }

    ID3D11Texture2D* GetTexture(uint32_t slot) {
        return (slot < 2) ? textures_[slot].Get() : nullptr;
    }

    bool LockSlot(uint32_t slot, DWORD timeoutMs = 5) {
        if (slot < 2 && keyedMutexes_[slot]) {
            return SUCCEEDED(keyedMutexes_[slot]->AcquireSync(0, timeoutMs));
        }
        return true;
    }

    void UnlockSlot(uint32_t slot) {
        if (slot < 2 && keyedMutexes_[slot]) {
            keyedMutexes_[slot]->ReleaseSync(1);
        }
        currentSlot_.store(slot, std::memory_order_release);
    }

private:
    bool isInitialized_{false};
    UINT width_{protocol::kWidth};
    UINT height_{protocol::kHeight};
    std::atomic<uint32_t> currentSlot_{0};

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> textures_[2];
    Microsoft::WRL::ComPtr<IDXGIKeyedMutex> keyedMutexes_[2];
    HANDLE sharedHandles_[2] = { nullptr, nullptr };
};

class DxgiTextureConsumer {
public:
    DxgiTextureConsumer() = default;
    ~DxgiTextureConsumer() { Close(); }

    bool Initialize(ID3D11Device* pDevice) {
        if (!pDevice) return false;
        Close();

        device_ = pDevice;
        Microsoft::WRL::ComPtr<ID3D11Device1> device1;
        pDevice->QueryInterface(IID_PPV_ARGS(&device1));

        for (int slot = 0; slot < 2; ++slot) {
            LPCWSTR name = (slot == 0) ? kDxgiSharedTexNameSlot0 : kDxgiSharedTexNameSlot1;
            HRESULT hr = E_FAIL;

            if (device1) {
                hr = device1->OpenSharedResourceByName(
                    name,
                    DXGI_SHARED_RESOURCE_READ,
                    IID_PPV_ARGS(&textures_[slot])
                );
            }

            if (FAILED(hr) || !textures_[slot]) {
                // If named open fails, we will try on demand
                textures_[slot].Reset();
            } else {
                textures_[slot].As(&keyedMutexes_[slot]);
            }
        }

        isInitialized_ = (textures_[0] || textures_[1]);
        return isInitialized_;
    }

    bool GetLatestTexture(
        uint32_t slot,
        Microsoft::WRL::ComPtr<ID3D11Texture2D>& outTexture
    ) {
        if (slot >= 2) slot = 0;

        if (!textures_[slot] && device_) {
            // Lazy re-open if publisher initialized after consumer
            Microsoft::WRL::ComPtr<ID3D11Device1> device1;
            if (SUCCEEDED(device_->QueryInterface(IID_PPV_ARGS(&device1))) && device1) {
                LPCWSTR name = (slot == 0) ? kDxgiSharedTexNameSlot0 : kDxgiSharedTexNameSlot1;
                device1->OpenSharedResourceByName(
                    name,
                    DXGI_SHARED_RESOURCE_READ,
                    IID_PPV_ARGS(&textures_[slot])
                );
                if (textures_[slot]) {
                    textures_[slot].As(&keyedMutexes_[slot]);
                }
            }
        }

        if (!textures_[slot]) return false;

        outTexture = textures_[slot];
        return true;
    }

    void Close() {
        for (int i = 0; i < 2; ++i) {
            keyedMutexes_[i].Reset();
            textures_[i].Reset();
        }
        device_.Reset();
        isInitialized_ = false;
    }

    bool IsInitialized() const { return isInitialized_; }

private:
    bool isInitialized_{false};
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> textures_[2];
    Microsoft::WRL::ComPtr<IDXGIKeyedMutex> keyedMutexes_[2];
};

} // namespace km::dxgi
