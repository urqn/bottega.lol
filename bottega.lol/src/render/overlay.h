#pragma once
#include <windows.h>
#include <atomic>
#include <cstdint>

namespace overlay {
    inline HWND hwnd = nullptr;
    inline HWND target = nullptr;
    inline bool menu_open = true;
    inline bool streamproof = false;
    inline bool vsync = false;
    inline bool running = true;

    // nanosecond timestamp of the last input event the hooks forwarded to the
    // overlay window; used by the main loop to keep the menu rendering at full
    // rate while the user interacts, then drop to a low idle cadence.
    inline std::atomic<std::int64_t> g_last_input_ns{ 0 };

    bool create();
    void destroy();
    void set_streamproof(bool on);
    void begin_frame();
    void end_frame();
    bool alive();
    void mark_input();
    bool input_recent();
}
