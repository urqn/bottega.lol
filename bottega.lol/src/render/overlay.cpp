#include "overlay.h"
#include "dx11.h"
#include "mem.h"

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#include "app/app.hpp"
#include "render/render.hpp"

#include <dwmapi.h>
#include <tlhelp32.h>
#include <chrono>
#include <string>
#include <cstdio>
#include <cctype>

#pragma comment(lib, "dwmapi.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace overlay {

static WNDCLASSEXW wc{};
static HHOOK kb_hook = nullptr;
static HHOOK ms_hook = nullptr;
static UINT resize_w = 0;
static UINT resize_h = 0;
static POINT last_mouse{ 0, 0 };
static POINT last_client{ -1, -1 };

	// while the menu is up we only intercept input when the game (or our own
	// overlay) still has foreground focus. if the user has switched to another
	// app - e.g. a screenshot tool like the Snipping Tool - we leave the input
	// alone so that app works normally.
	static bool input_owner_focused()
	{
		const HWND fg = GetForegroundWindow();
		return fg != nullptr && (fg == target || fg == hwnd);
	}

static LRESULT CALLBACK ll_mouse_proc(int code, WPARAM wparam, LPARAM lparam)
{
    if (code == HC_ACTION && menu_open && hwnd && input_owner_focused())
    {
        mark_input();

        const auto* ms = reinterpret_cast<MSLLHOOKSTRUCT*>(lparam);

        POINT pt = ms->pt;
        ScreenToClient(hwnd, &pt);
        const LPARAM lp = MAKELPARAM(pt.x, pt.y);

        switch (wparam)
        {
        case WM_MOUSEMOVE:
            if (!(ms->flags & LLMHF_INJECTED)) {
                // real hardware movement - this is the position the user wants,
                // so track it and feed a coalesced event to the menu (skip when
                // the client coords did not change to avoid flooding the message
                // pump and re-dispatch through the whole control tree).
                last_mouse = ms->pt;
                if (pt.x != last_client.x || pt.y != last_client.y) {
                    last_client = pt;
                    PostMessageW(hwnd, WM_MOUSEMOVE, 0, lp);
                }
                // returning 1 swallows the raw move so the game never sees it, but
                // it also freezes the OS cursor. re-apply the position here and in
                // alive() so the pointer tracks input and stays free of the clip /
                // capture the game restores. set to the same point does not raise a
                // new move, so this is not recursive.
                SetCursorPos(ms->pt.x, ms->pt.y);
                ClipCursor(nullptr);
            }
            else {
                // moves flagged LLMHF_INJECTED come from the game recapturing /
                // recentering the cursor every frame. ignore them for last_mouse
                // (otherwise the menu pointer is fought to the screen centre) but
                // swallow the game's input and snap straight back to the tracked
                // position so the menu cursor never visibly jumps.
                POINT cur{};
                if (GetCursorPos(&cur) && (cur.x != last_mouse.x || cur.y != last_mouse.y)) {
                    SetCursorPos(last_mouse.x, last_mouse.y);
                    ClipCursor(nullptr);
                }
            }
            return 1;
        case WM_LBUTTONDOWN:
            PostMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, lp);
            return 1;
        case WM_LBUTTONUP:
            PostMessageW(hwnd, WM_LBUTTONUP, 0, lp);
            return 1;
        case WM_RBUTTONDOWN:
            PostMessageW(hwnd, WM_RBUTTONDOWN, MK_RBUTTON, lp);
            return 1;
        case WM_RBUTTONUP:
            PostMessageW(hwnd, WM_RBUTTONUP, 0, lp);
            return 1;
        case WM_MBUTTONDOWN:
            PostMessageW(hwnd, WM_MBUTTONDOWN, MK_MBUTTON, lp);
            return 1;
        case WM_MBUTTONUP:
            PostMessageW(hwnd, WM_MBUTTONUP, 0, lp);
            return 1;
        case WM_XBUTTONDOWN:
            PostMessageW(hwnd, WM_XBUTTONDOWN, MAKEWPARAM(0, GET_XBUTTON_WPARAM(ms->mouseData)), lp);
            return 1;
        case WM_XBUTTONUP:
            PostMessageW(hwnd, WM_XBUTTONUP, MAKEWPARAM(0, GET_XBUTTON_WPARAM(ms->mouseData)), lp);
            return 1;
        case WM_MOUSEWHEEL:
            PostMessageW(hwnd, WM_MOUSEWHEEL, MAKEWPARAM(0, GET_WHEEL_DELTA_WPARAM(ms->mouseData)), lp);
            return 1;
        default:
            break;
        }
    }
    return CallNextHookEx(nullptr, code, wparam, lparam);
}

static LRESULT CALLBACK ll_keyboard_proc(int code, WPARAM wparam, LPARAM lparam)
{
if (code == HC_ACTION && hwnd)
    {
        mark_input();

        const auto* kb = reinterpret_cast<KBDLLHOOKSTRUCT*>(lparam);
        const UINT msg = (wparam == WM_KEYDOWN || wparam == WM_SYSKEYDOWN) ? WM_KEYDOWN : WM_KEYUP;
        const LPARAM lp = static_cast<LPARAM>(1u | (kb->scanCode << 16)
            | ((kb->flags & LLKHF_EXTENDED) ? (1u << 24) : 0u)
            | (msg == WM_KEYUP ? 0xC0000000u : 0u));

        // the menu toggle key is forwarded regardless of focus state - otherwise a
        // stale/missing target hwnd or an ambiguous foreground window leaves the
        // menu stuck open with no way back. modifier combos (ctrl+insert / alt /
        // win+insert) pass through untouched so OS shortcuts still fire.
        if (kb->vkCode == VK_INSERT && msg == WM_KEYDOWN)
        {
            const bool win  = (GetAsyncKeyState(VK_LWIN)  & 0x8000) != 0 || (GetAsyncKeyState(VK_RWIN)  & 0x8000) != 0;
            const bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
            const bool combo = win || ctrl || (kb->flags & LLKHF_ALTDOWN) != 0;
            if (!combo)
            {
                PostMessageW(hwnd, msg, static_cast<WPARAM>(kb->vkCode), lp);
                return 1;
            }
        }

        // while the menu is up the game must not receive input at all, so feed
        // our own window first and then swallow the key. modifier combos are
        // left alone (alt+tab/alt+f4 so the user isnt trapped, and printscreen /
        // win/crtl combos so screenshots and screen-record hotkeys still fire)
        const bool win  = (GetAsyncKeyState(VK_LWIN)  & 0x8000) != 0 || (GetAsyncKeyState(VK_RWIN)  & 0x8000) != 0;
        const bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool system_combo = (kb->flags & LLKHF_ALTDOWN) != 0 || win || ctrl || kb->vkCode == VK_SNAPSHOT;

        if (menu_open && input_owner_focused())
        {
            PostMessageW(hwnd, msg, static_cast<WPARAM>(kb->vkCode), lp);
            if (!system_combo) return 1;
        }
        else if (menu_open)
        {
            // the user is interacting with another app while the menu is up
            // (screenshot tool / alt-tab), let everything through untouched.
        }
        else
        {
            // if our own window happens to be focused the system already delivers
            // the key natively, posting it as well would input everything twice
            if (GetForegroundWindow() != hwnd)
                PostMessageW(hwnd, msg, static_cast<WPARAM>(kb->vkCode), lp);
        }
    }
    return CallNextHookEx(nullptr, code, wparam, lparam);
}

static LRESULT WINAPI wnd_proc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    app::on_wndproc(h, m, wp, lp);

    if (ImGui_ImplWin32_WndProcHandler(h, m, wp, lp)) return true;

    switch (m) {
        case WM_SIZE:
            if (wp != SIZE_MINIMIZED) {
                resize_w = LOWORD(lp);
                resize_h = HIWORD(lp);
            }
            return 0;
        case WM_SYSCOMMAND:
            if ((wp & 0xfff0) == SC_KEYMENU) return 0;
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            running = false;
            return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}


struct find_ctx { DWORD pid; HWND best; int area; bool need_vis; };

static BOOL CALLBACK enum_by_pid(HWND h, LPARAM lp)
{
    find_ctx* c = (find_ctx*)lp;
    if (c->need_vis && !IsWindowVisible(h)) return TRUE;

    DWORD wp = 0;
    GetWindowThreadProcessId(h, &wp);
    if (wp != c->pid) return TRUE;

    RECT rc;
    if (!GetClientRect(h, &rc)) return TRUE;
    int a = (rc.right - rc.left) * (rc.bottom - rc.top);
    int s = a > 0 ? a : 1;
    if (s > c->area) { c->area = s; c->best = h; }
    return TRUE;
}

struct tenum_ctx { HWND best; int area; };
static BOOL CALLBACK enum_thread_wnd(HWND h, LPARAM lp)
{
    tenum_ctx* c = (tenum_ctx*)lp;
    RECT rc;
    if (!GetClientRect(h, &rc)) return TRUE;
    int a = (rc.right - rc.left) * (rc.bottom - rc.top);
    int s = a > 0 ? a : 1;
    if (s > c->area) { c->area = s; c->best = h; }
    return TRUE;
}

static HWND find_by_thread_enum(DWORD pid)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return NULL;

    THREADENTRY32 te{};
    te.dwSize = sizeof(te);
    HWND best = NULL;
    int best_area = 0;

    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID != pid) continue;
            tenum_ctx tc{ NULL, 0 };
            EnumThreadWindows(te.th32ThreadID, enum_thread_wnd, (LPARAM)&tc);
            if (tc.area > best_area) { best_area = tc.area; best = tc.best; }
        }
        while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
    return best;
}

static std::string exe_name_lower(DWORD pid)
{
    if (!pid) return {};
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return {};
    PROCESSENTRY32W pe{}; pe.dwSize = sizeof(pe);
    std::string out;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (pe.th32ProcessID == pid) {
                for (const wchar_t* p = pe.szExeFile; *p; ++p)
                    out.push_back((char)tolower((unsigned char)(*p & 0x7F)));
                break;
            }
        }
        while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return out;
}

struct title_ctx { HWND best; int area; DWORD pid; HWND ignore; };

static BOOL CALLBACK enum_by_title(HWND h, LPARAM lp)
{
    title_ctx* c = (title_ctx*)lp;
    if (h == c->ignore) return TRUE;
    if (!IsWindowVisible(h)) return TRUE;

    char title[128] = { 0 };
    int len = GetWindowTextA(h, title, sizeof(title));
    if (len <= 0) return TRUE;

    int a = 0, b = len - 1;
    while (a <= b && (title[a] == ' ' || title[a] == '\t')) ++a;
    while (b >= a && (title[b] == ' ' || title[b] == '\t')) --b;
    int tl = b - a + 1;
    if (tl <= 0 || tl > 64) return TRUE;

    bool hit = false;
    for (int i = a; i + 6 <= a + tl; ++i) {
        if (_strnicmp(title + i, "roblox", 6) == 0) { hit = true; break; }
    }
    if (!hit) return TRUE;

    RECT rc;
    if (!GetClientRect(h, &rc)) return TRUE;
    int cw = rc.right - rc.left;
    int ch = rc.bottom - rc.top;
    if (cw < 300 || ch < 200) return TRUE;

    DWORD wp = 0;
    GetWindowThreadProcessId(h, &wp);
    if (!wp) return TRUE;

    std::string exe = exe_name_lower(wp);
    if (exe.rfind("roblox", 0) != 0) return TRUE;
    if (exe.find("studio") != std::string::npos) return TRUE;

    int ar = cw * ch;
    if (ar > c->area) { c->area = ar; c->best = h; c->pid = wp; }
    return TRUE;
}

static HWND find_by_title(DWORD* out_pid)
{
    title_ctx c{ NULL, 0, 0, hwnd };
    EnumWindows(enum_by_title, (LPARAM)&c);
    if (out_pid) *out_pid = c.pid;
    return c.best;
}

static HWND find_roblox()
{
    DWORD pid = mem::dwPid;

    if (pid != 0) {
        find_ctx c1{ pid, NULL, 0, true };
        EnumWindows(enum_by_pid, (LPARAM)&c1);
        if (c1.best) return c1.best;
    }

    {
        DWORD found = 0;
        HWND h = find_by_title(&found);
        if (h) {
            if (found && found != mem::dwPid) mem::dwPid = found;
            return h;
        }
    }

    if (pid != 0) {
        find_ctx c2{ pid, NULL, 0, false };
        EnumWindows(enum_by_pid, (LPARAM)&c2);
        if (c2.best) return c2.best;

        HWND h = find_by_thread_enum(pid);
        if (h) return h;
    }

    return NULL;
}


bool create()
{
    target = find_roblox();
    if (!target) { printf("[-] roblox window not found\n"); return false; }

    wc = { sizeof(wc), CS_CLASSDC, wnd_proc, 0, 0, GetModuleHandleW(NULL), NULL, NULL, NULL, NULL, L"bottega", NULL };
    RegisterClassExW(&wc);

    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);

    hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE,
        wc.lpszClassName, L"", WS_POPUP,
        0, 0, sw, sh,
        NULL, NULL, wc.hInstance, NULL);
    if (!hwnd) { printf("[-] CreateWindowExW failed err=%lu\n", GetLastError()); return false; }

    SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
    MARGINS m = { -1 };
    DwmExtendFrameIntoClientArea(hwnd, &m);

    if (!dx11::create(hwnd)) return false;

    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    UpdateWindow(hwnd);

    set_streamproof(streamproof);

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(dx11::device, dx11::context);

    app::device = dx11::device;

    render::setup();
    app::setup();

    kb_hook = SetWindowsHookExW(WH_KEYBOARD_LL, ll_keyboard_proc, GetModuleHandleW(nullptr), 0);
    if (!kb_hook) printf("[-] keyboard hook failed err=%lu\n", GetLastError());

    return true;
}

void set_streamproof(bool on)
{
    streamproof = on;
    if (!hwnd) return;
#ifdef WDA_EXCLUDEFROMCAPTURE
    SetWindowDisplayAffinity(hwnd, on ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE);
#else
    SetWindowDisplayAffinity(hwnd, on ? 0x11 : 0);
#endif
}

void destroy()
{
    if (kb_hook) { UnhookWindowsHookEx(kb_hook); kb_hook = nullptr; }
    if (ms_hook) { UnhookWindowsHookEx(ms_hook); ms_hook = nullptr; }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    dx11::destroy();

    if (hwnd) DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
}


bool alive()
{
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (!running) return false;

    if (!IsWindow(target)) {
        target = find_roblox();
        if (!target) return false;
    }

    RECT rc; GetClientRect(target, &rc);
    POINT tl{ 0, 0 }; ClientToScreen(target, &tl);
    int rw = rc.right - rc.left;
    int rh = rc.bottom - rc.top;

    RECT cur{}; GetWindowRect(hwnd, &cur);
    if (rw > 0 && rh > 0 && !IsIconic(target)) {
        if (cur.left != tl.x || cur.top != tl.y || (cur.right - cur.left) != rw || (cur.bottom - cur.top) != rh) {
            SetWindowPos(hwnd, HWND_TOPMOST, tl.x, tl.y, rw, rh, SWP_NOACTIVATE | SWP_NOSENDCHANGING);
            resize_w = rw;
            resize_h = rh;
        }
    }

    static bool last_open = false;
    if (menu_open != last_open)
    {
        last_open = menu_open;

        if (menu_open)
        {
            GetCursorPos(&last_mouse);
            SetCursorPos(last_mouse.x, last_mouse.y);
            ClipCursor(nullptr);
        }

        LONG ex = GetWindowLongW(hwnd, GWL_EXSTYLE);
        if (menu_open) SetWindowLongW(hwnd, GWL_EXSTYLE, ex & ~WS_EX_TRANSPARENT);
        else           SetWindowLongW(hwnd, GWL_EXSTYLE, ex |  WS_EX_TRANSPARENT);

        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);

        if (menu_open && !ms_hook)
            ms_hook = SetWindowsHookExW(WH_MOUSE_LL, ll_mouse_proc, GetModuleHandleW(nullptr), 0);
        else if (!menu_open && ms_hook)
        {
            UnhookWindowsHookEx(ms_hook);
            ms_hook = nullptr;
        }
    }

    // some games keep the cursor hidden / clipped / recaptured while they still
    // have focus, so while the menu is up re-assert the pointer every frame.
    // only while we own focus - otherwise a screenshot tool / other app the
    // user switched to would have its cursor fought against.
    if (menu_open && input_owner_focused())
    {
        SetCursorPos(last_mouse.x, last_mouse.y);
        ClipCursor(nullptr);
    }

    return running;
}

void begin_frame()
{
    if (resize_w != 0 && resize_h != 0) {
        dx11::resize(resize_w, resize_h);
        resize_w = resize_h = 0;
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void end_frame()
{
    ImGui::Render();

    const float clear[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    dx11::context->OMSetRenderTargets(1, &dx11::render_target, nullptr);
    dx11::context->ClearRenderTargetView(dx11::render_target, clear);
    dx11::context->OMSetBlendState(dx11::blend_state, nullptr, 0xFFFFFFFF);

    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    HRESULT hr = dx11::swap_chain->Present(vsync ? 1 : 0, 0);
    dx11::occluded = (hr == DXGI_STATUS_OCCLUDED);
}

void mark_input()
{
    using namespace std::chrono;
    g_last_input_ns.store(
        duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count(),
        std::memory_order_relaxed);
}

bool input_recent()
{
    using namespace std::chrono;
    const auto now = duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
    return (now - g_last_input_ns.load(std::memory_order_relaxed)) < 200000000LL;
}

}
