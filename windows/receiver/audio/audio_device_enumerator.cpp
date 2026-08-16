#include "audio_device_enumerator.h"
#include <wrl/client.h>
#include <algorithm>

namespace km::audio {

AudioDeviceEnumerator::AudioDeviceEnumerator() = default;

std::vector<AudioDevice> AudioDeviceEnumerator::EnumerateRenderDevices() {
    std::vector<AudioDevice> devices;

    Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
    if (FAILED(hr)) return devices;

    // Get default render endpoint
    std::wstring defaultId;
    Microsoft::WRL::ComPtr<IMMDevice> defaultDevice;
    if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &defaultDevice))) {
        LPWSTR pId = nullptr;
        if (SUCCEEDED(defaultDevice->GetId(&pId))) {
            defaultId = pId;
            CoTaskMemFree(pId);
        }
    }

    Microsoft::WRL::ComPtr<IMMDeviceCollection> collection;
    hr = enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection);
    if (FAILED(hr)) return devices;

    UINT count = 0;
    collection->GetCount(&count);

    for (UINT i = 0; i < count; ++i) {
        Microsoft::WRL::ComPtr<IMMDevice> device;
        if (FAILED(collection->Item(i, &device))) continue;

        LPWSTR pId = nullptr;
        if (FAILED(device->GetId(&pId))) continue;
        std::wstring id = pId;
        CoTaskMemFree(pId);

        Microsoft::WRL::ComPtr<IPropertyStore> props;
        std::wstring friendlyName = L"Audio Device";
        if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &props))) {
            PROPVARIANT varName;
            PropVariantInit(&varName);
            if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &varName))) {
                if (varName.vt == VT_LPWSTR && varName.pwszVal) {
                    friendlyName = varName.pwszVal;
                }
                PropVariantClear(&varName);
            }
        }

        AudioDevice d{};
        d.id = id;
        d.friendlyName = friendlyName;
        d.isDefault = (id == defaultId);

        std::wstring lowerName = friendlyName;
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::towlower);
        d.isCableInput = (lowerName.find(L"cable input") != std::wstring::npos);

        devices.push_back(d);
    }

    return devices;
}

std::optional<AudioDevice> AudioDeviceEnumerator::FindCableInputOrFallback() {
    auto devices = EnumerateRenderDevices();
    if (devices.empty()) return std::nullopt;

    // 1. Look for VB-CABLE "CABLE Input"
    for (const auto& d : devices) {
        if (d.isCableInput) {
            return d;
        }
    }

    // 2. Fallback to default render device
    for (const auto& d : devices) {
        if (d.isDefault) {
            return d;
        }
    }

    return devices.front();
}

} // namespace km::audio
