#pragma once

#include <atomic>
#include <windows.h>

namespace km::vcam {

inline std::atomic<ULONG> g_moduleObjectCount{0};

inline void ModuleObjectCreated() noexcept {
    ++g_moduleObjectCount;
}

inline void ModuleObjectDestroyed() noexcept {
    --g_moduleObjectCount;
}

} // namespace km::vcam
