#include "virtual_camera_registrar.h"
#include "../../virtual-camera/media-source/webrtc_bridge_guids.h"
#include <shellapi.h>

namespace km::vcam {

typedef HRESULT (STDAPICALLTYPE *MFCreateVirtualCameraFn)(
    MFVirtualCameraType type,
    MFVirtualCameraLifetime lifetime,
    MFVirtualCameraAccess access,
    LPCWSTR pwzFriendlyName,
    LPCWSTR pwzCLSID,
    const GUID* pCategories,
    ULONG cCategories,
    IMFVirtualCamera** ppVirtualCamera
);

VirtualCameraRegistrar::VirtualCameraRegistrar() = default;

VirtualCameraRegistrar::~VirtualCameraRegistrar() {
    StopVirtualCamera();
}

bool VirtualCameraRegistrar::StartVirtualCamera(const std::wstring& cameraFriendlyName) {
    if (isRunning_) return true;

    HRESULT hrMf = MFStartup(MF_VERSION);
    if (FAILED(hrMf)) {
        wprintf(L"  [VirtualCameraRegistrar::StartVirtualCamera] MFStartup failed hr=0x%08X\n", hrMf);
        return false;
    }

    HMODULE hMfSensor = GetModuleHandleW(L"mfsensorgroup.dll");
    if (!hMfSensor) {
        hMfSensor = LoadLibraryW(L"mfsensorgroup.dll");
    }
    if (!hMfSensor) {
        hMfSensor = LoadLibraryW(L"mfplat.dll");
    }
    if (!hMfSensor) {
        MFShutdown();
        return false;
    }

    auto pMFCreateVirtualCamera = reinterpret_cast<MFCreateVirtualCameraFn>(
        GetProcAddress(hMfSensor, "MFCreateVirtualCamera")
    );
    if (!pMFCreateVirtualCamera) {
        MFShutdown();
        return false;
    }

    HRESULT hr = pMFCreateVirtualCamera(
        MFVirtualCameraType_SoftwareCameraSource,
        MFVirtualCameraLifetime_Session,
        MFVirtualCameraAccess_CurrentUser,
        cameraFriendlyName.c_str(),
        kClsidString,
        nullptr,
        0,
        &virtualCamera_
    );

    if (FAILED(hr) || !virtualCamera_) {
        wprintf(L"  [VirtualCameraRegistrar::StartVirtualCamera] MFCreateVirtualCamera failed hr=0x%08X\n", hr);
        MFShutdown();
        return false;
    }

    hr = virtualCamera_->Start(nullptr);
    wprintf(L"  [VirtualCameraRegistrar::StartVirtualCamera] IMFVirtualCamera::Start hr=0x%08X\n", hr);
    if (FAILED(hr)) {
        virtualCamera_->Shutdown();
        virtualCamera_.Reset();
        MFShutdown();
        return false;
    }

    isRunning_ = true;
    return true;
}

void VirtualCameraRegistrar::StopVirtualCamera() {
    if (!isRunning_) return;

    if (virtualCamera_) {
        virtualCamera_->Stop();
        virtualCamera_->Shutdown();
        virtualCamera_.Reset();
    }
    isRunning_ = false;
    MFShutdown();
}

std::vector<CameraDeviceInfo> VirtualCameraRegistrar::EnumerateSystemCameras() {
    std::vector<CameraDeviceInfo> devices;
    if (FAILED(MFStartup(MF_VERSION))) return devices;

    IMFAttributes* pEnumAttrs = nullptr;
    HRESULT hr = MFCreateAttributes(&pEnumAttrs, 1);
    if (FAILED(hr) || !pEnumAttrs) {
        MFShutdown();
        return devices;
    }

    pEnumAttrs->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);

    IMFActivate** ppDevices = nullptr;
    UINT32 count = 0;
    hr = MFEnumDeviceSources(pEnumAttrs, &ppDevices, &count);
    if (SUCCEEDED(hr) && ppDevices) {
        for (UINT32 i = 0; i < count; ++i) {
            if (!ppDevices[i]) continue;
            CameraDeviceInfo info{};
            WCHAR name[256] = {0};
            UINT32 nameLen = 0;
            if (SUCCEEDED(ppDevices[i]->GetString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, name, 256, &nameLen))) {
                info.friendlyName = name;
            }

            WCHAR symlink[1024] = {0};
            UINT32 symlinkLen = 0;
            if (SUCCEEDED(ppDevices[i]->GetString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, symlink, 1024, &symlinkLen))) {
                info.symbolicLink = symlink;
            }

            if (info.friendlyName.find(L"WebRTC Bridge") != std::wstring::npos ||
                info.friendlyName.find(L"KM Virtual") != std::wstring::npos ||
                info.friendlyName.find(L"VirtualCameraMediaSource") != std::wstring::npos) {
                info.isVirtualCamera = true;
            }

            devices.push_back(std::move(info));
            ppDevices[i]->Release();
        }
        CoTaskMemFree(ppDevices);
    }

    pEnumAttrs->Release();

    // Deduplicate camera devices to prevent duplicate listings from multiple subsystem categories
    std::vector<CameraDeviceInfo> uniqueDevices;
    bool virtualCamAdded = false;
    for (auto& dev : devices) {
        if (dev.isVirtualCamera) {
            if (!virtualCamAdded) {
                dev.friendlyName = L"WebRTC Bridge Virtual Camera";
                uniqueDevices.push_back(std::move(dev));
                virtualCamAdded = true;
            }
        } else {
            bool exists = false;
            for (const auto& u : uniqueDevices) {
                if (u.friendlyName == dev.friendlyName) {
                    exists = true;
                    break;
                }
            }
            if (!exists) {
                uniqueDevices.push_back(std::move(dev));
            }
        }
    }

    MFShutdown();
    return uniqueDevices;
}

bool VirtualCameraRegistrar::IsVirtualCameraRegistered() {
    // 1. Check if enumerated as an active system video capture source
    auto cameras = EnumerateSystemCameras();
    for (const auto& cam : cameras) {
        if (cam.isVirtualCamera) return true;
    }

    // 2. Check HKLM COM registration
    HKEY hKey = nullptr;
    std::wstring clsidSubKey = L"SOFTWARE\\Classes\\CLSID\\" + std::wstring(kClsidString) + L"\\InProcServer32";
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, clsidSubKey.c_str(), 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        WCHAR szPath[MAX_PATH] = {0};
        DWORD dwSize = sizeof(szPath);
        if (RegQueryValueExW(hKey, nullptr, nullptr, nullptr, reinterpret_cast<LPBYTE>(szPath), &dwSize) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            if (GetFileAttributesW(szPath) != INVALID_FILE_ATTRIBUTES) {
                return true;
            }
        } else {
            RegCloseKey(hKey);
        }
    }

    // 3. Check HKCU COM registration
    std::wstring hkcuSubKey = L"Software\\Classes\\CLSID\\" + std::wstring(kClsidString) + L"\\InProcServer32";
    if (RegOpenKeyExW(HKEY_CURRENT_USER, hkcuSubKey.c_str(), 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        WCHAR szPath[MAX_PATH] = {0};
        DWORD dwSize = sizeof(szPath);
        if (RegQueryValueExW(hKey, nullptr, nullptr, nullptr, reinterpret_cast<LPBYTE>(szPath), &dwSize) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            if (GetFileAttributesW(szPath) != INVALID_FILE_ATTRIBUTES) {
                return true;
            }
        } else {
            RegCloseKey(hKey);
        }
    }

    return false;
}

bool VirtualCameraRegistrar::RegisterVirtualCameraWithElevation(HWND hWndParent) {
    WCHAR exePath[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    std::wstring exeDir = exePath;
    size_t lastSlash = exeDir.find_last_of(L"\\/");
    if (lastSlash != std::wstring::npos) {
        exeDir = exeDir.substr(0, lastSlash);
    }

    // Find register_vcam.ps1 script
    std::vector<std::wstring> scriptCandidates = {
        exeDir + L"\\..\\..\\..\\scripts\\register_vcam.ps1",
        exeDir + L"\\..\\..\\scripts\\register_vcam.ps1",
        exeDir + L"\\scripts\\register_vcam.ps1",
        exeDir + L"\\register_vcam.ps1",
        L"scripts\\register_vcam.ps1"
    };

    std::wstring scriptPath;
    for (const auto& path : scriptCandidates) {
        DWORD attribs = GetFileAttributesW(path.c_str());
        if (attribs != INVALID_FILE_ATTRIBUTES && !(attribs & FILE_ATTRIBUTE_DIRECTORY)) {
            WCHAR fullPath[MAX_PATH] = {0};
            if (GetFullPathNameW(path.c_str(), MAX_PATH, fullPath, nullptr)) {
                scriptPath = fullPath;
                break;
            }
        }
    }

    // Find VirtualCameraMediaSource.dll
    std::vector<std::wstring> dllCandidates = {
        exeDir + L"\\VirtualCameraMediaSource.dll",
        exeDir + L"\\Release\\VirtualCameraMediaSource.dll",
        exeDir + L"\\..\\..\\windows\\build\\Release\\VirtualCameraMediaSource.dll",
        L"C:\\ProgramData\\KMVirtualCamera\\VirtualCameraMediaSource.dll"
    };

    std::wstring dllPath;
    for (const auto& path : dllCandidates) {
        DWORD attribs = GetFileAttributesW(path.c_str());
        if (attribs != INVALID_FILE_ATTRIBUTES && !(attribs & FILE_ATTRIBUTE_DIRECTORY)) {
            WCHAR fullPath[MAX_PATH] = {0};
            if (GetFullPathNameW(path.c_str(), MAX_PATH, fullPath, nullptr)) {
                dllPath = fullPath;
                break;
            }
        }
    }

    if (scriptPath.empty()) {
        MessageBoxW(hWndParent, L"scripts/register_vcam.ps1 が見つかりませんでした。", L"仮想カメラ登録エラー", MB_OK | MB_ICONERROR);
        return false;
    }

    std::wstring params = L"-ExecutionPolicy Bypass -NoProfile -File \"" + scriptPath + L"\"";
    if (!dllPath.empty()) {
        params += L" -DllPath \"" + dllPath + L"\"";
    }

    SHELLEXECUTEINFOW shInfo{};
    shInfo.cbSize = sizeof(shInfo);
    shInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
    shInfo.hwnd = hWndParent;
    shInfo.lpVerb = L"runas";
    shInfo.lpFile = L"powershell.exe";
    shInfo.lpParameters = params.c_str();
    shInfo.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteExW(&shInfo)) {
        DWORD err = GetLastError();
        if (err == ERROR_CANCELLED) {
            // User cancelled UAC
            return false;
        }
        return false;
    }

    if (shInfo.hProcess) {
        WaitForSingleObject(shInfo.hProcess, 30000);
        DWORD exitCode = 0;
        GetExitCodeProcess(shInfo.hProcess, &exitCode);
        CloseHandle(shInfo.hProcess);
        return (exitCode == 0);
    }

    return true;
}

} // namespace km::vcam
