#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "vec.h"

namespace rbx {

    inline uintptr_t datamodel   = 0;
    inline uintptr_t local_player = 0;
    inline uintptr_t workspace   = 0;
    inline uintptr_t camera      = 0;
    inline uintptr_t visual_eng  = 0;

    struct Player {
        uintptr_t addr;
        uintptr_t character;
        uintptr_t hrp;
        uintptr_t humanoid;
        std::string name;
        std::string display;
        float health;
        float max_health;
        Vec3  pos;
        bool  friendly;
    };

    bool refresh();
    std::vector<Player> players();

    // cache-free live pointer resolvers for hot writer threads (aim/noclip/
    // fly/freecam). they re-drive the FakeDataModelPtr chain every call and
    // return 0 the moment a place/workspace tears down, so per-ms writers stop
    // touching freed instances instead of writing into recycled heap memory.
    uintptr_t fresh_local_player();
    uintptr_t fresh_camera();

    std::string  name_of(uintptr_t inst);
    std::string  classname(uintptr_t inst);
    uintptr_t    find_child(uintptr_t inst, const char* name);
    uintptr_t    find_child_byclass(uintptr_t inst, const char* cls);
    std::vector<uintptr_t> children(uintptr_t inst);

    Mat4  view_matrix();
    Vec2  viewport();
    bool  w2s(const Vec3& w, Vec2& s, const Mat4& mv, float sw, float sh);
}
