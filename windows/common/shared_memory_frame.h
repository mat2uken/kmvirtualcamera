#pragma once

#include <windows.h>
#include <sddl.h>
#include <stdio.h>
#include <cstdint>
#include <cstring>
#include <atomic>
#include <vector>
#include <span>
#include "frame_pipe_protocol.h"

#ifndef VCAM_LOGGER_DEFINED
#define VCAM_LOGGER_DEFINED
inline void LogVcam(const wchar_t* format, ...) {
    wchar_t buf[512] = {0};
    va_list args;
    va_start(args, format);
    _vsnwprintf_s(buf, sizeof(buf)/sizeof(wchar_t), _TRUNCATE, format, args);
    va_end(args);
    OutputDebugStringW(buf);

    FILE* f = nullptr;
    if (_wfopen_s(&f, L"C:\\ProgramData\\KMVirtualCamera\\vcam.log", L"a") == 0 && f) {
        fwprintf(f, L"%s\n", buf);
        fclose(f);
    }
}
#endif

namespace km::shm {

inline constexpr wchar_t kSharedFilePath[] = L"C:\\ProgramData\\KMVirtualCamera\\shared_frame.bin";
inline constexpr wchar_t kSharedEventName[] = L"Global\\KMVirtualCamera_NewFrameEvent";
inline constexpr uint32_t kSharedMemoryVersion = 3;

#pragma pack(push, 1)
struct FrameSlot {
    volatile LONG64 sequence;     // Slot sequence counter (odd: write in progress, even: valid)
    volatile LONG64 captureTimeUs;
    volatile ULONGLONG writeTick; // GetTickCount64() when written
    uint8_t payload[protocol::kPayloadBytes];
};

struct SharedMemoryHeader {
    uint8_t magic[8];              // 'W','R','T','C','V','F','0','3'
    uint32_t version;             // 3
    uint32_t width;               // 1280
    uint32_t height;              // 720
    uint32_t strideY;             // 1280
    uint32_t strideUV;            // 1280
    uint32_t payloadBytes;        // 1382400
    volatile uint32_t activeSlot; // Index of latest stable slot (0 or 1)
    uint32_t reserved;
    FrameSlot slots[2];           // Double-buffered frame slots
};
#pragma pack(pop)

inline constexpr size_t kSharedMemoryTotalSize = sizeof(SharedMemoryHeader);

// Writer class used by Receiver.exe and standalone feeder
class SharedMemoryPublisher {
public:
    SharedMemoryPublisher() = default;
    ~SharedMemoryPublisher() { Close(); }

    bool Open() {
        if (pBuffer_) return true;

        CreateDirectoryW(L"C:\\ProgramData\\KMVirtualCamera", nullptr);

        PSECURITY_DESCRIPTOR pSd = nullptr;
        SECURITY_ATTRIBUTES sa{};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = FALSE;

        // Permissive SDDL: Everyone (WD), AppContainer (AC), LocalService (LS), System (SY), AuthenticatedUsers (AU)
        if (ConvertStringSecurityDescriptorToSecurityDescriptorW(
                L"D:(A;;GA;;;WD)(A;;GA;;;AC)(A;;GA;;;LS)(A;;GA;;;SY)(A;;GA;;;AU)S:(ML;;NW;;;LW)",
                SDDL_REVISION_1, &pSd, nullptr)) {
            sa.lpSecurityDescriptor = pSd;
        }

        hEvent_ = CreateEventW(pSd ? &sa : nullptr, FALSE, FALSE, kSharedEventName);
        if (!hEvent_ && GetLastError() == ERROR_ALREADY_EXISTS) {
            hEvent_ = OpenEventW(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, kSharedEventName);
        }

        HANDLE hFile = CreateFileW(
            kSharedFilePath,
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            pSd ? &sa : nullptr,
            OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );

        if (pSd) LocalFree(pSd);
        if (hFile == INVALID_HANDLE_VALUE) return false;

        // Set file size to kSharedMemoryTotalSize
        LARGE_INTEGER liSize;
        liSize.QuadPart = static_cast<LONGLONG>(kSharedMemoryTotalSize);
        SetFilePointerEx(hFile, liSize, nullptr, FILE_BEGIN);
        SetEndOfFile(hFile);

        hMap_ = CreateFileMappingW(
            hFile,
            nullptr,
            PAGE_READWRITE,
            0,
            static_cast<DWORD>(kSharedMemoryTotalSize),
            nullptr
        );
        CloseHandle(hFile);

        if (!hMap_) return false;

        pBuffer_ = reinterpret_cast<uint8_t*>(MapViewOfFile(hMap_, FILE_MAP_ALL_ACCESS, 0, 0, kSharedMemoryTotalSize));
        if (!pBuffer_) {
            CloseHandle(hMap_);
            hMap_ = nullptr;
            return false;
        }

        // Initialize header if uninitialized
        SharedMemoryHeader* hdr = reinterpret_cast<SharedMemoryHeader*>(pBuffer_);
        std::memcpy(hdr->magic, "WRTCVF03", 8);
        hdr->version = kSharedMemoryVersion;
        hdr->width = protocol::kWidth;
        hdr->height = protocol::kHeight;
        hdr->strideY = protocol::kStrideY;
        hdr->strideUV = protocol::kStrideUv;
        hdr->payloadBytes = protocol::kPayloadBytes;
        hdr->activeSlot = 0;

        for (int i = 0; i < 2; ++i) {
            hdr->slots[i].sequence = 0;
            hdr->slots[i].captureTimeUs = 0;
            hdr->slots[i].writeTick = GetTickCount64();
            protocol::FillBlackNv12(std::span<uint8_t>(hdr->slots[i].payload, protocol::kPayloadBytes));
        }
        return true;
    }

    void PublishFrame(const uint8_t* nv12Data, size_t size, int64_t captureTimeUs) {
        if (!pBuffer_ && !Open()) return;
        if (!nv12Data || size != protocol::kPayloadBytes) return;

        SharedMemoryHeader* hdr = reinterpret_cast<SharedMemoryHeader*>(pBuffer_);

        // Double-buffering: write to back-buffer (slot opposite to activeSlot)
        uint32_t currentActive = hdr->activeSlot;
        uint32_t writeSlotIdx = (currentActive == 0) ? 1 : 0;
        FrameSlot* slot = &hdr->slots[writeSlotIdx];

        InterlockedIncrement64(&slot->sequence); // odd: writing
        MemoryBarrier();
        std::memcpy(slot->payload, nv12Data, protocol::kPayloadBytes);
        slot->captureTimeUs = captureTimeUs;
        slot->writeTick = GetTickCount64();
        MemoryBarrier();
        InterlockedIncrement64(&slot->sequence); // even: valid

        // Atomically flip active slot pointer
        InterlockedExchange(reinterpret_cast<volatile LONG*>(&hdr->activeSlot), writeSlotIdx);
        MemoryBarrier();

        // Signal consumer event immediately (Event-driven zero-latency wakeup)
        if (hEvent_) {
            SetEvent(hEvent_);
        }
    }

    void Close() {
        if (hEvent_) {
            CloseHandle(hEvent_);
            hEvent_ = nullptr;
        }
        if (pBuffer_) {
            UnmapViewOfFile(pBuffer_);
            pBuffer_ = nullptr;
        }
        if (hMap_) {
            CloseHandle(hMap_);
            hMap_ = nullptr;
        }
    }

private:
    HANDLE hMap_{nullptr};
    HANDLE hEvent_{nullptr};
    uint8_t* pBuffer_{nullptr};
};

// Reader class used by VirtualCameraMediaSource inside FrameServer / SystemSettings
class SharedMemoryConsumer {
public:
    SharedMemoryConsumer() = default;
    ~SharedMemoryConsumer() { Close(); }

    bool Open() {
        if (pBuffer_) return true;

        PSECURITY_DESCRIPTOR pSd = nullptr;
        SECURITY_ATTRIBUTES sa{};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = FALSE;

        if (ConvertStringSecurityDescriptorToSecurityDescriptorW(
                L"D:(A;;GA;;;WD)(A;;GA;;;AC)(A;;GA;;;LS)(A;;GA;;;SY)(A;;GA;;;AU)S:(ML;;NW;;;LW)",
                SDDL_REVISION_1, &pSd, nullptr)) {
            sa.lpSecurityDescriptor = pSd;
        }

        hEvent_ = CreateEventW(pSd ? &sa : nullptr, FALSE, FALSE, kSharedEventName);
        if (!hEvent_ && GetLastError() == ERROR_ALREADY_EXISTS) {
            hEvent_ = OpenEventW(SYNCHRONIZE, FALSE, kSharedEventName);
        }
        if (pSd) LocalFree(pSd);

        HANDLE hFile = CreateFileW(
            kSharedFilePath,
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );

        if (hFile == INVALID_HANDLE_VALUE) {
            return false;
        }

        hMap_ = CreateFileMappingW(
            hFile,
            nullptr,
            PAGE_READONLY,
            0,
            static_cast<DWORD>(kSharedMemoryTotalSize),
            nullptr
        );
        CloseHandle(hFile);

        if (!hMap_) return false;

        pBuffer_ = reinterpret_cast<const uint8_t*>(MapViewOfFile(hMap_, FILE_MAP_READ, 0, 0, kSharedMemoryTotalSize));
        if (!pBuffer_) {
            CloseHandle(hMap_);
            hMap_ = nullptr;
            return false;
        }

        return true;
    }

    HANDLE GetEventHandle() const {
        return hEvent_;
    }

    // Direct zero-copy access to the currently active frame buffer
    bool GetLatestFrameDirect(const uint8_t*& pOutPayload, uint64_t& outSequence, int64_t& outTimestampUs) {
        pOutPayload = nullptr;
        if (!pBuffer_ && !Open()) return false;
        if (!pBuffer_) return false;

        const SharedMemoryHeader* hdr = reinterpret_cast<const SharedMemoryHeader*>(pBuffer_);
        if (std::memcmp(hdr->magic, "WRTCVF03", 8) != 0 ||
            hdr->version != kSharedMemoryVersion ||
            hdr->width != protocol::kWidth || hdr->height != protocol::kHeight ||
            hdr->strideY != protocol::kStrideY || hdr->strideUV != protocol::kStrideUv ||
            hdr->payloadBytes != protocol::kPayloadBytes) {
            return false;
        }

        uint32_t activeIdx = hdr->activeSlot;
        if (activeIdx > 1) activeIdx = 0;

        const FrameSlot* slot = &hdr->slots[activeIdx];
        const ULONGLONG now = GetTickCount64();
        const ULONGLONG writeTick = slot->writeTick;
        if (now < writeTick || now - writeTick > 2000) {
            return false; // Stale (no active publisher)
        }

        pOutPayload = slot->payload;
        outSequence = static_cast<uint64_t>(slot->sequence / 2);
        outTimestampUs = static_cast<int64_t>(slot->captureTimeUs);
        return true;
    }

    // Backward-compatible copy method
    bool GetLatestFrame(std::vector<uint8_t>& outBuffer, uint64_t& outSequence, int64_t& outTimestampUs) {
        const uint8_t* pDirect = nullptr;
        if (!GetLatestFrameDirect(pDirect, outSequence, outTimestampUs) || !pDirect) {
            return false;
        }
        if (outBuffer.size() != protocol::kPayloadBytes) {
            outBuffer.resize(protocol::kPayloadBytes);
        }
        std::memcpy(outBuffer.data(), pDirect, protocol::kPayloadBytes);
        return true;
    }

    void Close() {
        if (hEvent_) {
            CloseHandle(hEvent_);
            hEvent_ = nullptr;
        }
        if (pBuffer_) {
            UnmapViewOfFile(pBuffer_);
            pBuffer_ = nullptr;
        }
        if (hMap_) {
            CloseHandle(hMap_);
            hMap_ = nullptr;
        }
    }

private:
    HANDLE hMap_{nullptr};
    HANDLE hEvent_{nullptr};
    const uint8_t* pBuffer_{nullptr};
};

} // namespace km::shm
