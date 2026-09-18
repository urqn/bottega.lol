#pragma once
#include <cstdint>
#include "vec.h"

namespace mv {

    inline bool  walk = false;
    inline float walk_value = 16.f;
    inline int   walk_key = 0;
    inline int   walk_mode = 0;

    inline bool  jump = false;
    inline float jump_value = 50.f;
    inline int   jump_key = 0;
    inline int   jump_mode = 0;

    inline bool  hip = false;
    inline float hip_value = 2.f;
    inline int   hip_key = 0;
    inline int   hip_mode = 0;

    inline bool  gravity = false;
    inline float gravity_value = 196.2f;
    inline int   gravity_key = 0;
    inline int   gravity_mode = 0;

    inline bool  fov = false;
    inline float fov_value = 70.f;
    inline int   fov_key = 0;
    inline int   fov_mode = 0;

    inline bool  bhop = false;
    inline float bhop_speed = 30.f;
    inline int   bhop_key = 0;
    inline int   bhop_mode = 0;

    inline bool  noclip = false;
    inline int   noclip_mode = 0;
    inline int   noclip_key = 0;
    inline int   noclip_key_mode = 0;

    inline bool  fly = false;
    inline float fly_speed = 50.f;
    inline float fly_damping = 10.f;
    inline float fly_vertical = 1.f;
    inline int   fly_key = 0;
    inline int   fly_mode = 0;

    inline bool  freecam = false;
    inline float freecam_speed = 1.5f;
    inline float freecam_sens = 0.003f;
    inline bool  freecam_freeze = false;
    inline int   freecam_key = 0;
    inline int   freecam_mode = 0;

inline bool fly_active = false;
    inline bool freecam_active = false;

    void spectate(std::uintptr_t target_humanoid);
    void unspectate();
    void teleport_to(const Vec3& pos);
    void teleport_to_player(std::uintptr_t target_hrp);

    void update();
    void shutdown();
}
