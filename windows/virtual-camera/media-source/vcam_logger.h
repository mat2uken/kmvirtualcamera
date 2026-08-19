#pragma once

#include <windows.h>
#include <stdio.h>

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
