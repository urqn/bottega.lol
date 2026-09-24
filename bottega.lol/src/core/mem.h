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

    // 1-byte probe: safe to call on any address; false on page fault / unmapped.
    inline bool is_valid(uintptr_t addr) {
        if (!hProc || !addr) return false;
        unsigned char probe = 0;
        SIZE_T rd = 0;
        return ReadProcessMemory(hProc, (LPCVOID)addr, &probe, 1, &rd) != 0 && rd == 1;
    }

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

    // Roblox string objects use the MSVC std::string layout: inline buffer
    // (<=15 chars) or heap pointer at +0x00, size at +0x10, capacity at +0x18.
    // For long strings a fresh RW page is carved in the target process so the
    // write never overflows a too-small heap buffer.
    bool write_string(uintptr_t addr, const std::string& val);

    bool read_block(uintptr_t addr, void* dst, size_t size);
}

