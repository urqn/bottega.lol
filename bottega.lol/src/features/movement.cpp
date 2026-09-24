#include "movement.h"
#include "check.h"
#include "mem.h"
#include "offsets.h"
#include "rbx.h"
#include "imgui.h"
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>


namespace mv {

namespace {

struct CFrame { float d[12]; };

struct Local {
    std::uintptr_t character{}, humanoid{}, hrp{}, prim{};
};

static Local local{};

static void resolve_local()
{
    if (!rbx::local_player) {
        local = Local{};
        return;
    }

    const std::uintptr_t ch = mem::read<std::uintptr_t>(rbx::local_player + off::ModelInstance);
    if (!ch) {
        local = Local{};
        return;
    }
    if (ch == local.character)
        return;

    local = Local{};
    local.character = ch;
    local.humanoid = rbx::find_child_byclass(ch, "Humanoid");
    local.hrp = rbx::find_child(ch, "HumanoidRootPart");
    if (local.hrp)
        local.prim = mem::read<std::uintptr_t>(local.hrp + off::Primitive);
}

static std::uintptr_t o_walk{}, o_walkchk{}, o_jump{}, o_jumph{}, o_usejump{}, o_hip{};
static std::uintptr_t o_world{}, o_grav{}, o_fov{}, o_cam_rot{}, o_cam_type{}, o_cam_subj{};
static std::uintptr_t o_vel{}, o_angvel{}, o_prim_rot{}, o_prim_flags{}, o_cancollide{}, o_anchored{};
static bool offsets_ready = false;

static void resolve_offsets()
{
    if (offsets_ready) return;
    offsets_ready = true;

    o_walk       = off::find("Humanoid.WalkSpeed");
    o_walkchk    = off::find("Humanoid.WalkspeedCheck");
    o_jump       = off::find("Humanoid.Jump");
    o_jumph      = off::find("Humanoid.JumpHeight");
    o_usejump    = off::find("Humanoid.UseJumpPower");
    o_hip        = off::find("Humanoid.HipHeight");
    o_world      = off::find("Workspace.World");
    o_grav       = off::find("World.Gravity");
    o_fov        = off::find("Camera.FieldOfView");
    o_cam_rot    = off::find("Camera.Rotation");
    o_cam_type   = off::find("Camera.CameraType");
    o_cam_subj   = off::find("Camera.CameraSubject");
    o_vel        = off::find("Primitive.AssemblyLinearVelocity");
    o_angvel     = off::find("Primitive.AssemblyAngularVelocity");
    o_prim_rot   = off::find("Primitive.Rotation");
    o_prim_flags = off::find("Primitive.Flags");
    o_cancollide = off::find("PrimitiveFlags.CanCollide");
    o_anchored   = off::find("PrimitiveFlags.Anchored");

    printf("[*] mv offsets  vel %llX ang %llX grav %llX camrot %llX primrot %llX flags %llX canc %llX anchor %llX world %llX\n",
        (unsigned long long)o_vel, (unsigned long long)o_angvel, (unsigned long long)o_grav,
        (unsigned long long)o_cam_rot, (unsigned long long)o_prim_rot, (unsigned long long)o_prim_flags,
        (unsigned long long)o_cancollide, (unsigned long long)o_anchored, (unsigned long long)o_world);
}

enum : int
{
    G_WALK = 0, G_JUMP, G_HIP, G_GRAV, G_FOV, G_BHOP, G_NOCLIP, G_FLY, G_FREECAM, G_COUNT
};

static bool key_down(int key)
{
    return key != 0 && (GetAsyncKeyState(key) & 0x8000) != 0;
}

static bool gate(int index, bool enabled, int key, int mode)
{
    static bool toggled[G_COUNT]{};
    static bool was_down[G_COUNT]{};
    static bool last_enabled[G_COUNT]{};
    static int  last_mode[G_COUNT]{};

    const int slot = index % G_COUNT;

    if (!enabled)
    {
        was_down[slot] = false;
        toggled[slot] = false;
        last_enabled[slot] = false;
        return false;
    }

    // entering toggle (or re-enabling the feature) while the key is already
    // held must not count as a fresh press, otherwise toggle latches on.
    if (!last_enabled[slot] || last_mode[slot] != mode)
    {
        toggled[slot] = false;
        was_down[slot] = mode == 1 ? key_down(key) : false;
        last_mode[slot] = mode;
        last_enabled[slot] = true;
    }

    if (mode == 2) return true;

    if (key == 0)
    {
        toggled[slot] = false;
        return mode == 0; // hold with no key = always-on, toggle with no key = off
    }

    if (check::blocked()) return false;

    const bool down = key_down(key);
    if (mode == 1)
    {
        if (down && !was_down[slot]) toggled[slot] = !toggled[slot];
        was_down[slot] = down;
        return toggled[slot];
    }

    was_down[slot] = down;
    return down;
}

static bool roblox_focused()
{
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    wchar_t cls[64]{};
    GetClassNameW(fg, cls, 63);
    return std::wcscmp(cls, L"Roblox") == 0 || std::wcscmp(cls, L"WINDOWSSCLIENT") == 0;
}

static std::uintptr_t world_ptr()
{
    if (!rbx::workspace || !o_world) return 0;
    return mem::read<std::uintptr_t>(rbx::workspace + o_world);
}

static void set_collide(std::uintptr_t part, bool collide)
{
    if (!part || !o_prim_flags || !o_cancollide) return;
    const std::uintptr_t prim = mem::read<std::uintptr_t>(part + off::Primitive);
    if (!prim) return;

    std::uint8_t flags = mem::read<std::uint8_t>(prim + o_prim_flags);
    const std::uint8_t bit = static_cast<std::uint8_t>(o_cancollide);
    const std::uint8_t next = collide ? static_cast<std::uint8_t>(flags | bit)
                                      : static_cast<std::uint8_t>(flags & ~bit);
    if (next != flags)
        mem::write<std::uint8_t>(prim + o_prim_flags, next);
}


// ---------------------------------------------------------------- local ticks

static void tick_walk()
{
    static bool was = false;
    static float backup = 16.f;
    static std::uintptr_t backup_hum = 0;

    if (gate(G_WALK, walk, 0, 2) && local.humanoid && o_walk) {
        if (!was || backup_hum != local.humanoid) {
            backup = mem::read<float>(local.humanoid + o_walk);
            if (!std::isfinite(backup) || backup <= 0.f) backup = 16.f;
            backup_hum = local.humanoid;
        }
        mem::write<float>(local.humanoid + o_walk, walk_value);
        if (o_walkchk) mem::write<float>(local.humanoid + o_walkchk, walk_value);
        was = true;
    }
    else if (was) {
        if (backup_hum && o_walk) {
            mem::write<float>(backup_hum + o_walk, backup);
            if (o_walkchk) mem::write<float>(backup_hum + o_walkchk, backup);
        }
        was = false;
    }
}

static void tick_jump()
{
    static bool was = false;
    static float backup_power = 50.f;
    static float backup_height = 7.2f;
    static std::uintptr_t backup_hum = 0;

    if (gate(G_JUMP, jump, 0, 2) && local.humanoid && o_jumph) {
        const std::uintptr_t hum = local.humanoid;
        const bool use_power = (o_usejump && o_jump) ? mem::read<bool>(hum + o_usejump) : false;

        if (!was || backup_hum != hum) {
            if (o_jump) {
                backup_power = mem::read<float>(hum + o_jump);
                if (!std::isfinite(backup_power) || backup_power <= 0.f) backup_power = 50.f;
            }
            backup_height = mem::read<float>(hum + o_jumph);
            if (!std::isfinite(backup_height) || backup_height <= 0.f) backup_height = 7.2f;
            backup_hum = hum;
        }

        if (use_power)
            mem::write<float>(hum + o_jump, jump_value);
        else
            mem::write<float>(hum + o_jumph, jump_value);

        was = true;
    }
    else if (was) {
        if (backup_hum) {
            mem::write<float>(backup_hum + o_jumph, backup_height);
            if (o_jump) mem::write<float>(backup_hum + o_jump, backup_power);
        }
        was = false;
    }
}

static void tick_hip()
{
    static bool was = false;
    static float backup = 2.f;
    static float last_written = -1.f;
    static std::uintptr_t backup_hum = 0;

    if (gate(G_HIP, hip, 0, 2) && local.humanoid && o_hip) {
        if (!was || backup_hum != local.humanoid) {
            backup = mem::read<float>(local.humanoid + o_hip);
            if (!std::isfinite(backup) || backup < 0.f) backup = 2.f;
            backup_hum = local.humanoid;
            last_written = -1.f;
        }
        if (hip_value != last_written) {
            mem::write<float>(local.humanoid + o_hip, hip_value);
            last_written = hip_value;
        }
        was = true;
    }
    else if (was) {
        if (backup_hum && o_hip)
            mem::write<float>(backup_hum + o_hip, backup);
        was = false;
        last_written = -1.f;
    }
}

static void tick_gravity()
{
    static bool was = false;
    static float backup = 196.2f;

    const std::uintptr_t world = world_ptr();
    if (gate(G_GRAV, gravity, 0, 2) && world && o_grav) {
        if (!was) {
            backup = mem::read<float>(world + o_grav);
            if (!std::isfinite(backup) || backup <= 0.f) backup = 196.2f;
        }
        mem::write<float>(world + o_grav, gravity_value);
        was = true;
    }
    else if (was) {
        if (world && o_grav) mem::write<float>(world + o_grav, backup);
        was = false;
    }
}

static void tick_fov()
{
    static bool was = false;
    static float backup = 70.f;

    if (gate(G_FOV, fov, 0, 2) && rbx::camera && o_fov) {
        if (!was) {
            backup = mem::read<float>(rbx::camera + o_fov);
            if (!std::isfinite(backup) || backup <= 0.f) backup = 70.f;
        }
        mem::write<float>(rbx::camera + o_fov, fov_value);
        was = true;
    }
    else if (was) {
        if (rbx::camera && o_fov) mem::write<float>(rbx::camera + o_fov, backup);
        was = false;
    }
}

static void tick_bhop()
{
    static bool was = false;
    static float backup = 16.f;
    static std::uintptr_t backup_hum = 0;

    const bool space = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
    if (gate(G_BHOP, bhop, 0, 2) && space && local.humanoid && o_walk) {
        if (!was || backup_hum != local.humanoid) {
            backup = mem::read<float>(local.humanoid + o_walk);
            if (!std::isfinite(backup) || backup <= 0.f) backup = 16.f;
            backup_hum = local.humanoid;
        }
        const float spd = (std::max)(bhop_speed, 0.f);
        mem::write<float>(local.humanoid + o_walk, spd);
        if (o_walkchk) mem::write<float>(local.humanoid + o_walkchk, spd);
        was = true;
    }
    else if (was) {
        if (backup_hum && o_walk) {
            mem::write<float>(backup_hum + o_walk, backup);
            if (o_walkchk) mem::write<float>(backup_hum + o_walkchk, backup);
        }
        was = false;
    }
}

static std::atomic<bool> nc_stop{ false };
static std::thread nc_thread;

static void noclip_worker()
{
    // dedicated high-frequency writer: the game re-asserts CanCollide every
    // physics tick, so a lone per-frame write loses the race and the shift
    // mode (hold/toggle/always) makes no difference. remember the original
    // state of every part and restore exactly that on deactivate.
    bool was = false;
    std::uintptr_t saved_for = 0;
    std::unordered_map<std::uintptr_t, bool> saved;

    // live top-of-chain: 0 once the local character is removed/destroyed.
    auto current_character = []() -> std::uintptr_t {
        const std::uintptr_t lpx = rbx::fresh_local_player();
        return lpx ? mem::read<std::uintptr_t>(lpx + off::ModelInstance) : 0;
        };

    auto restore_originals = [&] {
        // only restore parts that are still reachable under the same live
        // character. when the character is destroyed (match end) the primitives
        // are already gone; writing the saved flags back would corrupt whatever
        // the game allocated over the freed objects.
        if (saved_for && saved_for == current_character())
            for (const auto& entry : saved)
                set_collide(entry.first, entry.second);
        saved.clear();
        saved_for = 0;
        };

    while (!nc_stop.load(std::memory_order_acquire))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));

        const std::uintptr_t lp = rbx::fresh_local_player();
        const std::uintptr_t ch = lp ? mem::read<std::uintptr_t>(lp + off::ModelInstance) : 0;
        if (!gate(G_NOCLIP, noclip, noclip_key, noclip_key_mode) || !ch)
        {
            if (was) restore_originals();
            was = false;
            saved_for = 0;
            continue;
        }

        if (!was || saved_for != ch)
        {
            restore_originals();
            saved_for = ch;
            for (const std::uintptr_t part : rbx::children(ch)) {
                if (!part || !o_prim_flags || !o_cancollide) continue;
                const std::uintptr_t prim = mem::read<std::uintptr_t>(part + off::Primitive);
                if (!prim) continue;
                const std::uint8_t flags = mem::read<std::uint8_t>(prim + o_prim_flags);
                saved[part] = (flags & static_cast<std::uint8_t>(o_cancollide)) != 0;
            }
        }

        if (noclip_mode == 1)
        {
            if (const std::uintptr_t hrp = rbx::find_child(ch, "HumanoidRootPart"))
                set_collide(hrp, false);
        }
        else
        {
            for (const auto& entry : saved)
                set_collide(entry.first, false);
        }
        was = true;
    }

    restore_originals();
}

static void ensure_noclip()
{
    if (nc_thread.joinable()) return;
    nc_stop.store(false, std::memory_order_release);
    nc_thread = std::thread(noclip_worker);
}

struct Mat3
{
    float m[9]{};
};

static Vec3 rot_mul(const Mat3& r, const Vec3& v)
{
    // Roblox stores the CFrame rotation row-major, so the columns are the
    // right/up/back basis vectors and a column-vector multiply maps a local
    // move direction into world space.
    return {
        r.m[0] * v.x + r.m[1] * v.y + r.m[2] * v.z,
        r.m[3] * v.x + r.m[4] * v.y + r.m[5] * v.z,
        r.m[6] * v.x + r.m[7] * v.y + r.m[8] * v.z,
    };
}

static Vec3 vec3_normalized(const Vec3& v)
{
    const float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (len < 1e-6f) return v;
    return { v.x / len, v.y / len, v.z / len };
}

static std::atomic<bool> fly_stop{ false };
static std::thread fly_thread;

static void fly_restore_gravity()
{
    if (o_grav)
        if (const std::uintptr_t world = world_ptr())
            mem::write<float>(world + o_grav, 196.2f);
}

static void fly_worker()
{
    // the game re-asserts velocity/position against external writes, so each
    // tick spams the write a pile of times to out-race the re-assert.
    constexpr float k_speed = 100.0f;
    constexpr int k_spam = 2500;
    bool was_active = false;

    while (!fly_stop.load(std::memory_order_acquire))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        const bool active = gate(G_FLY, fly, fly_key, fly_mode) && rbx::camera;
        if (!active)
        {
            if (was_active)
            {
                fly_restore_gravity();
                was_active = false;
                fly_active = false;
            }
            continue;
        }
        was_active = true;
        fly_active = true;

        const std::uintptr_t lp = rbx::fresh_local_player();
        const std::uintptr_t ch = lp ? mem::read<std::uintptr_t>(lp + off::ModelInstance) : 0;
        const std::uintptr_t hrp = ch ? rbx::find_child(ch, "HumanoidRootPart") : 0;
        const std::uintptr_t prim = hrp ? mem::read<std::uintptr_t>(hrp + off::Primitive) : 0;
        if (!prim || !off::Position) continue;

        // during a 2500-write burst the character can be destroyed and its parts
        // freed (match end); re-check the live chain every so often and abort so
        // we never keep spraying a recycled instance.
        auto chain_alive = [&]() {
            const std::uintptr_t lpx = rbx::fresh_local_player();
            if (!lpx) return false;
            return mem::read<std::uintptr_t>(lpx + off::ModelInstance) != 0;
        };

        Mat3 rotation{};
        if (o_cam_rot) {
            if (const std::uintptr_t fc = rbx::fresh_camera())
                rotation = mem::read<Mat3>(fc + o_cam_rot);
        }

        auto kd = [](int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; };
        Vec3 move{ 0.f, 0.f, 0.f };
        bool is_moving = false;
        if (kd('W')) { move.z -= 1.f; is_moving = true; }
        if (kd('S')) { move.z += 1.f; is_moving = true; }
        if (kd('A')) { move.x -= 1.f; is_moving = true; }
        if (kd('D')) { move.x += 1.f; is_moving = true; }
        if (kd(VK_SPACE)) { move.y += 1.f; is_moving = true; }
        if (kd(VK_LCONTROL)) { move.y -= 1.f; is_moving = true; }

        const Vec3 current_position = mem::read<Vec3>(prim + off::Position);

        if (!is_moving)
        {
            // no input: hold the character in place against gravity/physics.
            if (fly_flight == 0 && o_grav)
            {
                if (const std::uintptr_t world = world_ptr())
                    mem::write<float>(world + o_grav, 0.f);
            }
            for (int i = 0; i < k_spam; ++i)
            {
                if ((i & 255) == 0 && !chain_alive()) break;
                mem::write<Vec3>(prim + off::Position, current_position);
                if (o_vel)
                    mem::write<Vec3>(prim + o_vel, Vec3{ 0.f, 0.f, 0.f });
            }
            continue;
        }

        move = vec3_normalized(move);
        const Vec3 dir = rot_mul(rotation, move);

        if (fly_flight == 0)
        {
            if (o_grav)
            {
                if (const std::uintptr_t world = world_ptr())
                    mem::write<float>(world + o_grav, 0.f);
            }
            const Vec3 new_velocity = dir * k_speed;
            for (int i = 0; i < k_spam; ++i)
            {
                if ((i & 255) == 0 && !chain_alive()) break;
                if (o_vel)
                    mem::write<Vec3>(prim + o_vel, new_velocity);
                if (o_angvel)
                    mem::write<Vec3>(prim + o_angvel, Vec3{ 0.f, 0.f, 0.f });
            }
        }
        else if (fly_flight == 1)
        {
            const Vec3 new_position = current_position + (dir * (k_speed / 165.0f));
            for (int i = 0; i < k_spam; ++i)
            {
                if ((i & 255) == 0 && !chain_alive()) break;
                mem::write<Vec3>(prim + off::Position, new_position);
                if (o_vel)
                    mem::write<Vec3>(prim + o_vel, Vec3{ 0.f, 0.f, 0.f });
            }
        }
        else
        {
            const Mat3 current_rotation = mem::read<Mat3>(prim + o_prim_rot);
            const Vec3 new_position = current_position + (dir * (k_speed / 165.0f));
            for (int i = 0; i < k_spam; ++i)
            {
                if ((i & 255) == 0 && !chain_alive()) break;
                mem::write<Vec3>(prim + off::Position, new_position);
                if (o_prim_rot)
                    mem::write<Mat3>(prim + o_prim_rot, current_rotation);
                if (o_vel)
                    mem::write<Vec3>(prim + o_vel, Vec3{ 0.f, 0.f, 0.f });
            }
        }
    }

    fly_restore_gravity();
    fly_active = false;
}

static void ensure_fly()
{
    if (fly_thread.joinable()) return;
    fly_stop.store(false, std::memory_order_release);
    fly_thread = std::thread(fly_worker);
}


// ------------------------------------------------------------------- freecam

static std::atomic<bool> cam_running{ false };
static std::thread cam_thread;
static std::mutex cam_mutex;
static CFrame cam_target{};
static bool cam_freeze = false;
static std::uintptr_t cam_hum = 0;
static std::uintptr_t cam_prim = 0;
static CFrame cam_player_cframe{};
static bool cam_player_anchored = false;
static float cam_saved_walk = 16.f;
static float cam_saved_walkchk = 16.f;
static int cam_saved_type = 5;
static std::uintptr_t cam_saved_subject = 0;
static CFrame cam_saved_cframe{};

static CFrame build_cframe(float pitch, float yaw, const Vec3& pos)
{
    const float cy = std::cos(yaw), sy = std::sin(yaw);
    const float cp = std::cos(pitch), sp = std::sin(pitch);
    CFrame cf{};
    cf.d[0] = cy;      cf.d[1] = sy * sp;  cf.d[2] = sy * cp;
    cf.d[3] = 0.f;     cf.d[4] = cp;       cf.d[5] = -sp;
    cf.d[6] = -sy;     cf.d[7] = cy * sp;  cf.d[8] = cy * cp;
    cf.d[9] = pos.x;   cf.d[10] = pos.y;   cf.d[11] = pos.z;
    return cf;
}

static void cframe_to_pitch_yaw(const CFrame& cf, float& pitch, float& yaw)
{
    const float sy = -cf.d[6];
    const float cy = std::sqrt(cf.d[0] * cf.d[0] + cf.d[3] * cf.d[3]);
    yaw = std::atan2(sy, cy);
    pitch = std::atan2(-cf.d[5], cf.d[4]);
}

// cache-free check that `humanoid` is still a child of the given character, so
// freecam never keeps writing to a stale humanoid after the character is
// destroyed and replaced (cached lookups would return the freed snapshot).
static bool character_owns_humanoid(std::uintptr_t character, std::uintptr_t humanoid)
{
    if (!character || !humanoid) return false;
    for (const std::uintptr_t c : rbx::children(character))
        if (c == humanoid) return true;
    return false;
}

static void cam_loop()
{
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);

    while (cam_running.load()) {
        CFrame target{};
        bool freeze = false;
        std::uintptr_t hum = 0;
        std::uintptr_t prim = 0;
        CFrame player{};
        {
            std::lock_guard<std::mutex> lock(cam_mutex);
            target = cam_target;
            freeze = cam_freeze;
            hum = cam_hum;
            prim = cam_prim;
            player = cam_player_cframe;
        }

        if (const std::uintptr_t cam = rbx::fresh_camera()) {
            if (o_cam_type) 
                mem::write<int>(cam + o_cam_type, 6);
            if (o_cam_subj) 
                mem::write<std::uintptr_t>(cam + o_cam_subj, 0);
            mem::write<CFrame>(cam + o_cam_rot, target);
        }

        if (freeze) {
            // the local character may have been destroyed (match end); only
            // keep freezing it while the same live character is still up.
            const std::uintptr_t lpx = rbx::fresh_local_player();
            const std::uintptr_t cur_ch = (lpx && hum) ? mem::read<std::uintptr_t>(lpx + off::ModelInstance) : 0;
            const bool char_alive = cur_ch != 0 && character_owns_humanoid(cur_ch, hum);

            if (char_alive && hum && o_walk) {
                mem::write<float>(hum + o_walk, 0.f);
                if (o_walkchk) mem::write<float>(hum + o_walkchk, 0.f);
            }
            if (char_alive && prim && o_prim_rot) {
                mem::write<CFrame>(prim + o_prim_rot, player);
                if (o_vel) mem::write<Vec3>(prim + o_vel, Vec3{});
                if (o_prim_flags && o_anchored) {
                    std::uint8_t flags = mem::read<std::uint8_t>(prim + o_prim_flags);
                    flags = static_cast<std::uint8_t>(flags | static_cast<std::uint8_t>(o_anchored));
                    mem::write<std::uint8_t>(prim + o_prim_flags, flags);
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

static void freecam_stop()
{
    cam_running.store(false);
    if (cam_thread.joinable()) cam_thread.join();

    // only restore when the camera still exists; after a match-end teardown the
    // cached rbx::camera is a freed object and writing the saved cframe back
    // would corrupt recycled game memory.
    if (const std::uintptr_t cam = rbx::fresh_camera()) {
        if (o_cam_type) mem::write<int>(cam + o_cam_type, cam_saved_type);
        if (o_cam_subj) mem::write<std::uintptr_t>(cam + o_cam_subj, cam_saved_subject);
        if (o_cam_rot)  mem::write<CFrame>(cam + o_cam_rot, cam_saved_cframe);
    }

    const std::uintptr_t cur_ch = [&]() -> std::uintptr_t {
        const std::uintptr_t lpx = rbx::fresh_local_player();
        return lpx ? mem::read<std::uintptr_t>(lpx + off::ModelInstance) : 0;
        }();
    const bool char_same = cam_prim != 0 && cur_ch != 0 && character_owns_humanoid(cur_ch, cam_hum);

    if (char_same && cam_prim && o_prim_flags && o_anchored) {
        std::uint8_t flags = mem::read<std::uint8_t>(cam_prim + o_prim_flags);
        if (cam_player_anchored)
            flags = static_cast<std::uint8_t>(flags | static_cast<std::uint8_t>(o_anchored));
        else
            flags = static_cast<std::uint8_t>(flags & ~static_cast<std::uint8_t>(o_anchored));
        mem::write<std::uint8_t>(cam_prim + o_prim_flags, flags);
        if (o_vel) mem::write<Vec3>(cam_prim + o_vel, Vec3{});
    }

    if (char_same && cam_hum && o_walk) {
        mem::write<float>(cam_hum + o_walk, cam_saved_walk);
        if (o_walkchk) mem::write<float>(cam_hum + o_walkchk, cam_saved_walkchk);
    }

    cam_hum = 0;
    cam_prim = 0;
    cam_player_anchored = false;
    cam_freeze = false;
    freecam_active = false;
}

static void tick_freecam()
{
    static bool was = false;
    static float pitch = 0.f, yaw = 0.f;
    static Vec3 position{};
    static bool has_center = false;

    const bool active = gate(G_FREECAM, freecam, freecam_key, freecam_mode) && rbx::workspace && rbx::camera;

    if (!active) {
        if (was) freecam_stop();
        was = false;
        has_center = false;
        return;
    }

    if (!was) {
        if (!o_cam_rot) return;

        cam_saved_type = o_cam_type ? mem::read<int>(rbx::camera + o_cam_type) : 5;
        cam_saved_subject = o_cam_subj ? mem::read<std::uintptr_t>(rbx::camera + o_cam_subj) : 0;
        cam_saved_cframe = mem::read<CFrame>(rbx::camera + o_cam_rot);

        position = Vec3{ cam_saved_cframe.d[9], cam_saved_cframe.d[10], cam_saved_cframe.d[11] };
        if (position.x == 0.f && position.y == 0.f && position.z == 0.f) {
            if (local.prim && off::Position) {
                const Vec3 rp = mem::read<Vec3>(local.prim + off::Position);
                position = Vec3{ rp.x, rp.y + 2.f, rp.z };
            }
        }

        cframe_to_pitch_yaw(cam_saved_cframe, pitch, yaw);

        cam_prim = local.prim;
        cam_hum = local.humanoid;
        cam_player_anchored = false;
        if (cam_prim && o_prim_rot)
            cam_player_cframe = mem::read<CFrame>(cam_prim + o_prim_rot);

        if (o_walk && cam_hum) {
            cam_saved_walk = mem::read<float>(cam_hum + o_walk);
            if (!std::isfinite(cam_saved_walk) || cam_saved_walk <= 0.f) cam_saved_walk = 16.f;
            cam_saved_walkchk = o_walkchk ? mem::read<float>(cam_hum + o_walkchk) : cam_saved_walk;
            if (!std::isfinite(cam_saved_walkchk) || cam_saved_walkchk <= 0.f) cam_saved_walkchk = cam_saved_walk;
        }
        if (cam_prim && o_prim_flags && o_anchored)
            cam_player_anchored = (mem::read<std::uint8_t>(cam_prim + o_prim_flags) & static_cast<std::uint8_t>(o_anchored)) != 0;

        cam_freeze = freecam_freeze;

        {
            std::lock_guard<std::mutex> lock(cam_mutex);
            cam_target = build_cframe(pitch, yaw, position);
        }

        cam_running.store(true);
        cam_thread = std::thread(cam_loop);
        freecam_active = true;
        was = true;
        return;
    }

    const bool rmb = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
    const int center_x = GetSystemMetrics(SM_CXSCREEN) / 2;
    const int center_y = GetSystemMetrics(SM_CYSCREEN) / 2;

    if (rmb) {
        POINT cur{};
        GetCursorPos(&cur);
        if (!has_center) {
            SetCursorPos(center_x, center_y);
            has_center = true;
        }
        else {
            const float dx = static_cast<float>(cur.x - center_x);
            const float dy = static_cast<float>(cur.y - center_y);
            if (dx != 0.f || dy != 0.f) {
                yaw -= dx * freecam_sens;
                pitch -= dy * freecam_sens;
                pitch = std::clamp(pitch, -1.48f, 1.48f);
            }
            SetCursorPos(center_x, center_y);
        }
    }
    else {
        has_center = false;
    }

    const float dt = std::clamp(ImGui::GetIO().DeltaTime, 0.001f, 0.05f);
    const float move = freecam_speed * dt * 60.f;

    const float c_yaw = std::cos(yaw), s_yaw = std::sin(yaw);
    const float c_pitch = std::cos(pitch), s_pitch = std::sin(pitch);
    const Vec3 fwd{ -s_yaw * c_pitch, s_pitch, -c_yaw * c_pitch };
    const Vec3 right{ c_yaw, 0.f, -s_yaw };

    auto kd = [](int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; };
    if (kd('W')) { position.x += fwd.x * move; position.y += fwd.y * move; position.z += fwd.z * move; }
    if (kd('S')) { position.x -= fwd.x * move; position.y -= fwd.y * move; position.z -= fwd.z * move; }
    if (kd('A')) { position.x -= right.x * move; position.y -= right.y * move; position.z -= right.z * move; }
    if (kd('D')) { position.x += right.x * move; position.y += right.y * move; position.z += right.z * move; }
    if (kd(VK_SPACE)) position.y += move;
    if (kd(VK_CONTROL)) position.y -= move;

    {
        std::lock_guard<std::mutex> lock(cam_mutex);
        cam_target = build_cframe(pitch, yaw, position);
        cam_freeze = freecam_freeze;
    }
}

} // namespace


// ----------------------------------------------------------------- spectate

// phantomX-style spectate: point the camera at the target's Humanoid via
// CameraSubject + CameraType. one-shot writes, no follow thread, no per-frame
// rotation spam - the engine tracks the assigned subject itself.
static std::uintptr_t spec_target{ 0 };
static int spec_saved_type = 5;
static std::uintptr_t spec_saved_subject = 0;

void spectate(std::uintptr_t target_humanoid)
{
    if (cam_running.load())
        freecam_stop();

    if (spec_target == target_humanoid) {
        unspectate();
        return;
    }

    if (!target_humanoid || !rbx::camera) return;

    spec_saved_type = o_cam_type ? mem::read<int>(rbx::camera + o_cam_type) : 5;
    spec_saved_subject = o_cam_subj ? mem::read<std::uintptr_t>(rbx::camera + o_cam_subj) : 0;

    if (o_cam_type) mem::write<int>(rbx::camera + o_cam_type, 5); // Enum.CameraType.Custom
    if (o_cam_subj) mem::write<std::uintptr_t>(rbx::camera + o_cam_subj, target_humanoid);
    spec_target = target_humanoid;
}

void unspectate()
{
    if (!spec_target) return;

    if (rbx::camera) {
        if (o_cam_type) mem::write<int>(rbx::camera + o_cam_type, spec_saved_type);
        if (o_cam_subj && spec_saved_subject)
            mem::write<std::uintptr_t>(rbx::camera + o_cam_subj, spec_saved_subject);
    }
    spec_target = 0;
}

// ----------------------------------------------------------------- teleport (held writes)

namespace
{
    bool      tp_active = false;
    std::uintptr_t tp_prim = 0;
    Vec3      tp_pos{};
    int       tp_frames = 0;
    constexpr int k_tp_frames = 180;
}

static void apply_tp_write()
{
    if (!tp_prim || !o_prim_rot) return;

    CFrame cf = mem::read<CFrame>(tp_prim + o_prim_rot);
    cf.d[9] = tp_pos.x;
    cf.d[10] = tp_pos.y;
    cf.d[11] = tp_pos.z;
    mem::write<CFrame>(tp_prim + o_prim_rot, cf);
    if (o_vel) mem::write<Vec3>(tp_prim + o_vel, Vec3{});
}

static void tick_teleport()
{
    if (!tp_active || tp_frames <= 0)
    {
        tp_active = false;
        return;
    }

    apply_tp_write();
    --tp_frames;
}

void teleport_to(const Vec3& pos)
{
    resolve_local();
    if (!local.prim || !o_prim_rot) return;

    tp_prim = local.prim;
    tp_pos = pos;
    tp_frames = k_tp_frames;
    tp_active = true;

    apply_tp_write();
}

void teleport_to_player(std::uintptr_t target_hrp)
{
    resolve_local();
    if (!local.prim || !o_prim_rot) return;

    // phantomX-style: hop to the target's live position, materialising just
    // above their feet (+3 studs) so we land beside them.
    const std::uintptr_t tprim = target_hrp ? mem::read<std::uintptr_t>(target_hrp + off::Primitive) : 0;
    const Vec3 tpos = tprim ? mem::read<Vec3>(tprim + off::Position) : Vec3{};
    if (tpos.x == 0.0f && tpos.y == 0.0f && tpos.z == 0.0f) return;

    tp_prim = local.prim;
    tp_pos = Vec3{ tpos.x, tpos.y + 3.0f, tpos.z };
    tp_frames = k_tp_frames;
    tp_active = true;

    apply_tp_write();
}


void update()
{
    resolve_offsets();
    resolve_local();

    tick_walk();
    tick_jump();
    tick_hip();
    tick_gravity();
    tick_fov();
    tick_bhop();
    ensure_noclip();
    ensure_fly();
    tick_freecam();
    tick_teleport();
}

void shutdown()
{
    tp_active = false;
    tp_prim = 0;

    fly_stop.store(true, std::memory_order_release);
    if (fly_thread.joinable()) fly_thread.join();

    nc_stop.store(true, std::memory_order_release);
    if (nc_thread.joinable()) nc_thread.join();

    if (cam_running.load())
        freecam_stop();
    spec_target = 0;
}

}
