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

#include "toast.h"
#include "trigger.h"
#include "hbe.h"
#include "hit.h"
#include "cosmetic.h"
#include "world.h"
#include "waypoints.h"
#include "perf.h"
#include "serverbrowser.h"
#include "check.h"
#include "freeze.h"
#include "imgui.h"


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
    std::shared_ptr<std::vector<rbx::Player>> plist_src;
    bool overlay_visible = true;     // window starts shown by overlay::create

    for (;;) {
        const auto t0 = std::chrono::steady_clock::now();
        if (!overlay::alive()) break;

        {
            std::lock_guard<std::mutex> lk(g_scan_mutex);
            // the scan thread swaps in a fresh snapshot at ~30Hz; copy only when
            // it actually changed, not on every frame (players carry strings).
            std::shared_ptr<std::vector<rbx::Player>> cur = g_scan_playlist;
            if (cur && cur != plist_src) {
                plist = *cur;
                plist_src = cur;
            }
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

        // lazily spin up the ported feature workers; each start() is idempotent
        // and only proceeds once, so toggles picked up later still take effect.
        trigger::start();
        hbe::start();
        hit::start();
        cosmetic::start();
        world::start();
        check::start();
        freeze::start();

        // content is wanted whenever any always-on-screen visual is enabled -
        // the menu window, esp boxes, the watermark or the aim fov circles. the
        // draw functions gate themselves, so calling them with everything off is
        // a no-op; this just decides whether to run the render/present pass at all.
        const bool want_watermark = app::watermark_enabled();
        const bool want_aim_visuals = aim::draw_fov || aim::draw_silent_fov || aim::tracer;
        const bool want_hit_visuals = hit::markers_enabled;
        const bool want_wp_visuals = wp::render_enabled;
        const bool want_overlay = perf::stats;
        const bool want_toasts = toast::wants_draw();
        // real-time visuals only matter while the game actually owns the
        // foreground: when alt-tabbed away they would render a stale snapshot
        // every frame for nothing. esp/aim count as "live" only in-game, so the
        // overlay stops being fed (and stops consuming gpu) the moment they're not.
        const HWND fg = GetForegroundWindow();
        const bool in_game = (fg == overlay::target) || (fg == overlay::hwnd);
        const bool esp_live = esp::enabled && in_game;
        const bool aim_visuals_live = want_aim_visuals && in_game;
        const bool need_content = overlay::menu_open || esp_live || want_aim_visuals || want_watermark
            || want_hit_visuals || want_wp_visuals || want_overlay || want_toasts;

        if (need_content)
        {
            if (!overlay_visible) {
                ShowWindow(overlay::hwnd, SW_SHOWNOACTIVATE);
                overlay_visible = true;
            }
            overlay::begin_frame();
            aim::draw();
            esp::draw(plist);

            const Mat4 vm = rbx::view_matrix();
            const Vec2 dims{ ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y };
            if (dims.x > 0.0f && dims.y > 0.0f)
            {
                if (hit::markers_enabled)
                    hit::render(ImGui::GetBackgroundDrawList(), vm, dims);
                wp::render(ImGui::GetBackgroundDrawList(), vm, dims);
                perf::render(ImGui::GetBackgroundDrawList(), plist, dims);
            }

            app::render();
            toast::draw();
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
            || esp_live || aim_visuals_live;

        // the menu itself idles at 15fps when untouched (message wait wakes it
        // on any queued input); it earns the full refresh-rate budget only while
        // the cursor is actually live on it, so an open menu barely costs cpu.
        const bool full_rate = overlay::input_recent();

        const double fps = (boosted && need_content)
            ? (full_rate ? (dx11::refresh_rate > 0.0 ? dx11::refresh_rate : 60.0) : 60.0)
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
    trigger::shutdown();
    hbe::shutdown();
    hit::shutdown();
    cosmetic::shutdown();
    world::shutdown();
    check::shutdown();
    freeze::shutdown();
    overlay::destroy();
    mem::detach();
    return 0;
}
