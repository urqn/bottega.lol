#include "movement.h"
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

    const int slot = index % G_COUNT;

    if (!enabled)
    {
        was_down[slot] = key_down(key);
        toggled[slot] = false;
        return false;
    }

    if (mode == 2 || key == 0) return true;

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

static std::vector<std::uintptr_t> character_parts()
{
    std::vector<std::uintptr_t> out;
    if (!local.character) return out;

    static const char* r15_names[] = {
        "HumanoidRootPart", "Head", "UpperTorso", "LowerTorso",
        "LeftUpperArm", "LeftLowerArm", "LeftHand", "RightUpperArm", "RightLowerArm", "RightHand",
        "LeftUpperLeg", "LeftLowerLeg", "LeftFoot", "RightUpperLeg", "RightLowerLeg", "RightFoot"
    };
    static const char* r6_names[] = {
        "HumanoidRootPart", "Head", "Torso", "Left Arm", "Right Arm", "Left Leg", "Right Leg"
    };

    const bool r6 = (rbx::find_child(local.character, "UpperTorso") == 0);
    const char** names = r6 ? r6_names : r15_names;
    const std::size_t count = r6 ? (sizeof(r6_names) / sizeof(*r6_names))
                                 : (sizeof(r15_names) / sizeof(*r15_names));

    for (std::size_t i = 0; i < count; ++i) {
        if (std::uintptr_t p = rbx::find_child(local.character, names[i]))
            out.push_back(p);
    }
    return out;
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

    if (gate(G_WALK, walk, walk_key, walk_mode) && local.humanoid && o_walk) {
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

    if (gate(G_JUMP, jump, jump_key, jump_mode) && local.humanoid && o_jumph) {
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

    if (gate(G_HIP, hip, hip_key, hip_mode) && local.humanoid && o_hip) {
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
    if (gate(G_GRAV, gravity, gravity_key, gravity_mode) && world && o_grav) {
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

    if (gate(G_FOV, fov, fov_key, fov_mode) && rbx::camera && o_fov) {
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
    if (gate(G_BHOP, bhop, bhop_key, bhop_mode) && space && local.humanoid && o_walk) {
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

static void tick_noclip()
{
    static bool was = false;
    static std::uintptr_t built_for = 0;
    static std::vector<std::uintptr_t> parts{};

    if (gate(G_NOCLIP, noclip, noclip_key, noclip_key_mode) && local.character) {
        if (!was || built_for != local.character) {
            parts = character_parts();
            built_for = local.character;
        }

        if (noclip_mode == 1) {
            if (local.hrp) set_collide(local.hrp, false);
        }
        else {
            for (auto p : parts) set_collide(p, false);
        }
        was = true;
    }
    else if (was) {
        for (auto p : parts) set_collide(p, true);
        parts.clear();
        built_for = 0;
        was = false;
    }
}

static void tick_fly()
{
    static bool was = false;
    static bool grav_over = false;
    static float grav_backup = 0.f;
    static Vec3 cur_vel{};
    static auto last = std::chrono::steady_clock::now();

    auto restore_gravity = [&] {
        if (grav_over) {
            if (const std::uintptr_t world = world_ptr())
                mem::write<float>(world + o_grav, grav_backup);
            grav_over = false;
        }
    };

    const auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - last).count();
    last = now;
    if (!(dt >= 0.0001f)) dt = 0.0001f;

    const bool active = gate(G_FLY, fly, fly_key, fly_mode) && local.prim && rbx::camera;

    if (!active) {
        if (was && local.prim && o_vel) {
            mem::write<Vec3>(local.prim + o_vel, Vec3{ 0.f, 0.f, 0.f });
            if (o_angvel) mem::write<Vec3>(local.prim + o_angvel, Vec3{ 0.f, 0.f, 0.f });
        }
        cur_vel = Vec3{};
        was = false;
        fly_active = false;
        restore_gravity();
        return;
    }

    was = true;
    fly_active = true;

    if (!grav_over) {
        if (const std::uintptr_t world = world_ptr()) {
            grav_backup = mem::read<float>(world + o_grav);
            grav_over = true;
        }
    }
    if (grav_over) {
        if (const std::uintptr_t world = world_ptr())
            mem::write<float>(world + o_grav, 0.f);
    }

    float rot[9]{};
    if (o_cam_rot) {
        for (int i = 0; i < 9; ++i)
            rot[i] = mem::read<float>(rbx::camera + o_cam_rot + static_cast<std::uint64_t>(i) * sizeof(float));
    }

    Vec3 fwd{ -rot[2], -rot[5], -rot[8] };
    Vec3 right{ -rot[0], rot[3], -rot[6] };

    auto norm = [](Vec3& v, const Vec3& fallback) {
        const float m = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
        if (m < 1e-6f) { v = fallback; return; }
        v.x /= m; v.y /= m; v.z /= m;
    };
    norm(fwd, Vec3{ 0.f, 0.f, 1.f });
    norm(right, Vec3{ 1.f, 0.f, 0.f });

    fwd.y = 0.f;
    norm(fwd, Vec3{ 0.f, 0.f, 1.f });

    const float spd = (std::max)(fly_speed, 0.f);
    const float vspd = spd * fly_vertical;
    Vec3 target{};

    if (roblox_focused()) {
        auto kd = [](int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; };
        if (kd('W')) target = target + fwd * spd;
        if (kd('S')) target = target - fwd * spd;
        if (kd('A')) target = target + right * spd;
        if (kd('D')) target = target - right * spd;

        float vert = 0.f;
        if (kd(VK_SPACE)) vert += 1.f;
        if (kd(VK_CONTROL)) vert -= 1.f;
        if (vert != 0.f) target = target + Vec3{ 0.f, 1.f, 0.f } * (vert * vspd);
    }

    const float damping = (std::max)(fly_damping, 0.f);
    const float cdt = std::clamp(dt, 0.001f, 0.05f);
    if (damping > 0.f) {
        const float alpha = 1.0f - std::exp(-damping * cdt);
        cur_vel = cur_vel + (target - cur_vel) * alpha;
    }
    else {
        cur_vel = target;
    }

    if (o_vel) mem::write<Vec3>(local.prim + o_vel, cur_vel);
    if (o_angvel) mem::write<Vec3>(local.prim + o_angvel, Vec3{ 0.f, 0.f, 0.f });

    // throttled flight diagnostic: one line per second while active showing the
    // resolved primitive, the gravity value actually in world, the movement
    // vectors and whether the write target is being applied.
    static auto diag_last = std::chrono::steady_clock::now();
    if (now - diag_last >= std::chrono::seconds(1))
    {
        diag_last = now;
        const Vec3 pos = (local.prim && off::Position)
            ? mem::read<Vec3>(local.prim + off::Position) : Vec3{};
        float g = 0.f;
        if (const std::uintptr_t world = world_ptr())
            g = mem::read<float>(world + o_grav);
        printf("[fly] prim %llX vel %llX grav %llX cam %llX worldgrav %.1f fwd %.2f %.2f %.2f rgt %.2f %.2f %.2f tgt %.2f %.2f %.2f cur %.2f %.2f %.2f pos %.2f %.2f %.2f\n",
            (unsigned long long)local.prim, (unsigned long long)o_vel, (unsigned long long)o_grav,
            (unsigned long long)rbx::camera, g,
            fwd.x, fwd.y, fwd.z, right.x, right.y, right.z,
            target.x, target.y, target.z, cur_vel.x, cur_vel.y, cur_vel.z, pos.x, pos.y, pos.z);
        fflush(stdout);
    }
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

        if (rbx::workspace && o_cam_rot) {
            const std::uintptr_t cam = mem::read<std::uintptr_t>(rbx::workspace + off::Camera);
            if (cam) {
                if (o_cam_type) mem::write<int>(cam + o_cam_type, 6);
                if (o_cam_subj) mem::write<std::uintptr_t>(cam + o_cam_subj, 0);
                mem::write<CFrame>(cam + o_cam_rot, target);
            }
        }

        if (freeze) {
            if (hum && o_walk) {
                mem::write<float>(hum + o_walk, 0.f);
                if (o_walkchk) mem::write<float>(hum + o_walkchk, 0.f);
            }
            if (prim && o_prim_rot) {
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

    if (rbx::workspace && rbx::camera && o_cam_rot) {
        if (o_cam_type) mem::write<int>(rbx::camera + o_cam_type, cam_saved_type);
        if (o_cam_subj) mem::write<std::uintptr_t>(rbx::camera + o_cam_subj, cam_saved_subject);
        mem::write<CFrame>(rbx::camera + o_cam_rot, cam_saved_cframe);
    }

    if (cam_prim && o_prim_flags && o_anchored) {
        std::uint8_t flags = mem::read<std::uint8_t>(cam_prim + o_prim_flags);
        if (cam_player_anchored)
            flags = static_cast<std::uint8_t>(flags | static_cast<std::uint8_t>(o_anchored));
        else
            flags = static_cast<std::uint8_t>(flags & ~static_cast<std::uint8_t>(o_anchored));
        mem::write<std::uint8_t>(cam_prim + o_prim_flags, flags);
        if (o_vel) mem::write<Vec3>(cam_prim + o_vel, Vec3{});
    }

    if (cam_hum && o_walk) {
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
    tick_noclip();
    tick_fly();
    tick_freecam();
    tick_teleport();
}

void shutdown()
{
    tp_active = false;
    tp_prim = 0;

    if (cam_running.load())
        freecam_stop();
    spec_target = 0;
}

}
