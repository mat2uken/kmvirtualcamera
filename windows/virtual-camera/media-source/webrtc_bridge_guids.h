#pragma once

#include <guiddef.h>

// CLSID for WebRtcBridge Virtual Camera Media Source: {84BA9D22-C5E5-4674-8848-A979BD2764B2}
// {84BA9D22-C5E5-4674-8848-A979BD2764B2}
inline constexpr GUID CLSID_WebRtcBridgeVirtualCameraMediaSource = {
    0x84ba9d22, 0xc5e5, 0x4674, { 0x88, 0x48, 0xa9, 0x79, 0xbd, 0x27, 0x64, 0xb2 }
};

inline constexpr const wchar_t* kClsidString = L"{84BA9D22-C5E5-4674-8848-A979BD2764B2}";
inline constexpr const wchar_t* kFriendlyName = L"WebRTC Bridge Virtual Camera";
