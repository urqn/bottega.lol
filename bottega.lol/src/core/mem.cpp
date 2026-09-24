#include "mem.h"
#include <cstring>
#include <tlhelp32.h>


namespace mem {

static DWORD find_pid(const wchar_t* name)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32W e{};
    e.dwSize = sizeof(e);
    DWORD hit = 0;
    if (Process32FirstW(snap, &e)) {
        do {
            if (_wcsicmp(e.szExeFile, name) == 0) { hit = e.th32ProcessID; break; }
        }
        while (Process32NextW(snap, &e));
    }

    CloseHandle(snap);
    return hit;
}


static uintptr_t find_base(DWORD p, std::wstring* out_path)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32, p);
    if(snap == INVALID_HANDLE_VALUE) return 0;

    MODULEENTRY32W m{};
    m.dwSize = sizeof(m);
    uintptr_t b = 0;

    if (Module32FirstW(snap, &m)) {
        b = (uintptr_t)m.modBaseAddr;
        if (out_path) *out_path = m.szExePath;
    }
    CloseHandle(snap);
    return b;
}
bool attach(const wchar_t* name) {
    dwPid = find_pid(name);
    if (!dwPid) return false;

    hProc = OpenProcess(PROCESS_VM_READ|PROCESS_VM_WRITE|PROCESS_VM_OPERATION|PROCESS_QUERY_INFORMATION, FALSE, dwPid);
    if (!hProc) return false;

    base = find_base(dwPid, &exe_path);
    return base != 0;
}

void detach()
{
    if (hProc) CloseHandle(hProc);
    hProc = NULL;
    dwPid = 0;
    base  = 0;
    exe_path.clear();
}

std::string extract_version(const std::wstring& path) {
    auto pos = path.find(L"version-");
    if (pos == std::wstring::npos) return {};
    auto end = path.find(L'\\', pos);
    auto slice = (end == std::wstring::npos) ? path.substr(pos) : path.substr(pos, end - pos);

    std::string out;
    out.reserve(slice.size());


    for (wchar_t c : slice) out.push_back((char)(c & 0x7F));
    return out;
}


std::string read_string(uintptr_t addr) {
    if (!addr || !hProc) return {};
    char buf[128]{};
    if (!ReadProcessMemory(hProc, (LPCVOID)addr, buf, sizeof(buf) - 1, NULL)) return {};
    return std::string(buf);
}
std::string read_lenstr(uintptr_t addr)
{
    if (!addr || !hProc) return {};

    // MSVC std::string: inline data (<=15 chars) or ptr at +0x00, size at +0x10
    size_t len = read<size_t>(addr + 0x10);
    if (len == 0 || len > 256) return {};

    uintptr_t data = (len >= 16) ? read<uintptr_t>(addr) : addr;
    if (!data) return {};

    std::string s(len, '\0');
    if (!ReadProcessMemory(hProc, (LPCVOID)data, &s[0], len, NULL)) return {};
    return s;
}

bool read_block(uintptr_t addr, void* dst, size_t size)
{
    if (!addr || !dst || !size || !hProc) return false;
    return ReadProcessMemory(hProc, (LPCVOID)addr, dst, size, NULL) != 0;
}

bool write_string(uintptr_t addr, const std::string& val)
{
    if (!addr || !hProc || addr < 0x10000) return false;

    const size_t len = val.size();

    // short strings live inline in the 16-byte union at +0x00.
    if (len < 16)
    {
        char buf[16]{};
        std::memcpy(buf, val.data(), len);
        if (!WriteProcessMemory(hProc, (LPVOID)addr, buf, 16, NULL)) return false;
        if (!write<size_t>(addr + 0x10, len)) return false;
        if (!write<size_t>(addr + 0x18, 0)) return false; // _Myres 0 <=> cap 15
        return true;
    }

    const size_t cur_res = read<size_t>(addr + 0x18);
    const size_t cur_cap = (cur_res + 1) * 16 - 1;
    const uintptr_t cur_ptr = read<uintptr_t>(addr);

    // reuse the existing heap buffer when it is big enough.
    if (cur_ptr != addr && cur_ptr >= 0x10000 && cur_cap >= len)
    {
        if (!WriteProcessMemory(hProc, (LPVOID)cur_ptr, val.data(), len + 1, NULL)) return false;
        if (!write<size_t>(addr + 0x10, len)) return false;
        return true;
    }

    // carve a fresh page for the payload and install it into the string object.
    LPVOID page = VirtualAllocEx(hProc, nullptr, len + 1, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!page) return false;

    if (!WriteProcessMemory(hProc, page, val.data(), len + 1, NULL))
    {
        VirtualFreeEx(hProc, page, 0, MEM_RELEASE);
        return false;
    }
    if (!WriteProcessMemory(hProc, (LPVOID)(addr + 0x00), &page, sizeof(page), NULL))
    {
        VirtualFreeEx(hProc, page, 0, MEM_RELEASE);
        return false;
    }
    if (!write<size_t>(addr + 0x10, len))
    {
        VirtualFreeEx(hProc, page, 0, MEM_RELEASE);
        return false;
    }
    const size_t res = (len + 1) / 16 + 1; // reported cap >= len
    if (!write<size_t>(addr + 0x18, res))
    {
        VirtualFreeEx(hProc, page, 0, MEM_RELEASE);
        return false;
    }
    return true;
}

}
