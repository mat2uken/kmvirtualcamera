#pragma once

#include <windows.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <string>
#include <vector>
#include <optional>

namespace km::audio {

struct AudioDevice {
    std::wstring id;
    std::wstring friendlyName;
    bool isDefault = false;
    bool isCableInput = false;
};

class AudioDeviceEnumerator {
public:
    AudioDeviceEnumerator();
    ~AudioDeviceEnumerator() = default;

    std::vector<AudioDevice> EnumerateRenderDevices();
    std::optional<AudioDevice> FindCableInputOrFallback();
};

} // namespace km::audio
