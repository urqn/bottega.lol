#include <windows.h>
#include <chrono>
#include <thread>
#include <mutex>
#include <memory>
#include <atomic>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "mem.h"
#include "offsets.h"
#include "rbx.h"
#include "dx11.h"
#include "overlay.h"
#include "aim.h"
#include "esp.h"
#include "movement.h"
#include <app/app.hpp>
#include <config/config.hpp>


static bool bootstrap()
{
    printf("[*] waiting for RobloxPlayerBeta.exe ...\n");
    while (!mem::attach(L"RobloxPlayerBeta.exe")) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    printf("[+] pid %lu bottega.lol 0x%llX\n", mem::dwPid, (unsigned long long)mem::base);

    std::string ver = mem::extract_version(mem::exe_path);
    if (ver.empty()) { printf("[-] version tag missing in path\n"); return false; }
    printf("[+] %s\n", ver.c_str());

    if (!off::fetch(ver)) { printf("[-] bottega.lol offsets fetch failed\n"); return false; }
    printf("[+] %zu offsets\n", off::map.size());

    if (!overlay::create()) { printf("[-] overlay init failed\n"); return false; }
    printf("[+] overlay up\n");
    return true;
}


// The datamodel re-walk + player enumeration is the heaviest memory read burst,
// so it runs on its own thread at ~30Hz. The render thread only copies the latest
// snapshot, keeping the menu, input and esp drawing free of scan stalls.
static std::mutex               g_scan_mutex;
static std::shared_ptr<std::vector<rbx::Player>> g_scan_playlist;
static std::atomic<bool>        g_scan_running{ true };
static std::thread              g_scan_thread;

static void scan_loop()
{
    bool diag = true;
    auto next_scan = std::chrono::steady_clock::now();

    while (g_scan_running.load(std::memory_order_acquire))
    {
        const auto now = std::chrono::steady_clock::now();
        if (now >= next_scan)
        {
            rbx::refresh();
            auto fresh = std::make_shared<std::vector<rbx::Player>>(rbx::players());

            size_t n = 0;
            {
                std::lock_guard<std::mutex> lk(g_scan_mutex);
                g_scan_playlist = std::move(fresh);
                n = g_scan_playlist->size();
            }

            if (diag) {
                diag = false;
                printf("[*] rbx : dm 0x%llX ws 0x%llX cam 0x%llX lp 0x%llX ve 0x%llX players %zu\n",
                    (unsigned long long)rbx::datamodel, (unsigned long long)rbx::workspace,
                    (unsigned long long)rbx::camera, (unsigned long long)rbx::local_player,
                    (unsigned long long)rbx::visual_eng, n);
            }

            // esp boxes and the aimbot want a fresh snapshot in real time; when
            // nothing consumes it we only re-scan so the players list stays warm.
            const bool fast = esp::enabled || aim::enabled || aim::silent;
            next_scan = now + (fast ? std::chrono::milliseconds(33) : std::chrono::milliseconds(130));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}


int main()
{
    SetConsoleTitleW(L"bottega.lol");
    if (!bootstrap()) {
        printf("\n[*] startup failed - press any key to close...\n");
        system("pause >nul");
        return 1;
    }

    g_scan_thread = std::thread(scan_loop);

    std::vector<rbx::Player> plist;
    bool overlay_visible = true;     // window starts shown by overlay::create

    for (;;) {
        const auto t0 = std::chrono::steady_clock::now();
        if (!overlay::alive()) break;

        {
            std::lock_guard<std::mutex> lk(g_scan_mutex);
            if (g_scan_playlist) plist = *g_scan_playlist;
        }

        // menu_open is our single source of truth for "is the menu up" and must
        // track the window state even while the menu is hidden (render is
        // skipped then, so the old sync inside app::render can never fire).
        if (!app::windows.empty())
            overlay::menu_open = app::windows[0]->m_opened;

        app::sync_features();
        mv::update();
        aim::update(plist);
        app::set_players(plist);

        // content is wanted whenever any always-on-screen visual is enabled -
        // the menu window, esp boxes, the watermark or the aim fov circles. the
        // draw functions gate themselves, so calling them with everything off is
        // a no-op; this just decides whether to run the render/present pass at all.
        const bool want_watermark = app::watermark_enabled();
        const bool want_aim_visuals = aim::draw_fov || aim::draw_silent_fov || aim::tracer;
        const bool need_content = overlay::menu_open || esp::enabled || want_watermark || want_aim_visuals;

        if (need_content)
        {
            if (!overlay_visible) {
                ShowWindow(overlay::hwnd, SW_SHOWNOACTIVATE);
                overlay_visible = true;
            }
            overlay::begin_frame();
            aim::draw();
            esp::draw(plist);
            app::render();
            overlay::end_frame();
        }
        else if (overlay_visible)
        {
            // nothing to draw: hide the fullscreen window so dwm has nothing to
            // composite (cheaper than presenting a transparent frame, and it
            // guarantees the last menu frame cannot linger on screen).
            ShowWindow(overlay::hwnd, SW_HIDE);
            overlay_visible = false;
        }

        // gate the framerate. real-time features (esp boxes, aim fov visuals)
        // keep the full refresh-rate budget; the menu by itself is only animate
        // for a short pulse after input, so once it settles we idle at ~15fps
        // to keep cpu/gpu near zero while it sits open.
        static auto g_boost_until = std::chrono::steady_clock::now() + std::chrono::milliseconds(400);
        static bool g_prev_menu_open = overlay::menu_open;
        if (overlay::menu_open != g_prev_menu_open) {
            g_prev_menu_open = overlay::menu_open;
            g_boost_until = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
        }
        if (overlay::input_recent())
            g_boost_until = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);

        const bool boosted = std::chrono::steady_clock::now() < g_boost_until
            || esp::enabled || want_aim_visuals;

        const double fps = (boosted && need_content)
            ? (dx11::refresh_rate > 0.0 ? dx11::refresh_rate : 60.0)
            : 15.0;
        const auto budget = std::chrono::milliseconds(static_cast<long long>(1000.0 / fps));
        const auto remain = budget - (std::chrono::steady_clock::now() - t0);
        if (remain > std::chrono::milliseconds(1))
        {
            const DWORD block = need_content
                ? static_cast<DWORD>(std::chrono::duration_cast<std::chrono::milliseconds>(remain).count())
                : 30; // idle: sleep longer, nothing to draw

            // message-aware wait instead of Sleep so any queued input (the LL
            // hook messages, posted mouse/key events) wakes the loop instantly -
            // the menu reacts to the cursor in real time even while idle-framed.
            MsgWaitForMultipleObjectsEx(0, nullptr, block, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        }
    }

    g_scan_running.store(false, std::memory_order_release);
    if (g_scan_thread.joinable()) g_scan_thread.join();

    mv::shutdown();
    aim::shutdown();
    overlay::destroy();
    mem::detach();
    return 0;
}
