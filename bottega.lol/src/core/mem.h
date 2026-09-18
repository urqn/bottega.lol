#pragma once
#include <windows.h>
#include <cstdint>
#include <string>

namespace mem {

    inline HANDLE     hProc    = NULL;
    inline DWORD      dwPid    = 0;
    inline uintptr_t  base     = 0;
    inline std::wstring exe_path;

    bool attach(const wchar_t* name);
    void detach();

    std::string extract_version(const std::wstring& path);

    template <typename T>
    T read(uintptr_t addr) {
        T v{};
        if (hProc) ReadProcessMemory(hProc, (LPCVOID)addr, &v, sizeof(T), NULL);
        return v;
    }

    template <typename T>
    bool write(uintptr_t addr, const T& v) {
        if (!hProc) return false;
        return WriteProcessMemory(hProc, (LPVOID)addr, &v, sizeof(T), NULL) != 0;
    }

    std::string read_string(uintptr_t addr);
    std::string read_lenstr(uintptr_t addr);

    bool read_block(uintptr_t addr, void* dst, size_t size);
}

