#pragma once
#include "rbx.h"
#include <cstdint>
#include <vector>

namespace esp {
    inline bool  enabled = true;
    inline bool  self = false;
    inline bool  friendly = true;
    inline float max_dist = 1500.f;

    inline bool  box = true;
    inline int   box_mode = 0;  // 0 = bounding (2D), 1 = corner, 2 = 3D (phantomx)
    inline int   bounding_type = 0; // 0 = parts (OBB union), 1 = mesh (ExpandBounds)
    inline bool  box_outline = true; // black back-outline
    inline float box_thickness = 1.0f;
    inline bool  box_filled = false;
    inline bool  box_fill_gradient = true;

    inline bool  health_bar = false;
    inline bool  name_tag = true;
    inline bool  distance = false;
    inline bool  tool = false;
    inline bool  flags = false;
    inline int   flag_sel = 0x3F;

    inline bool  head_dot = false;
    inline float head_dot_size = 3.f;
    inline bool  view_direction = false;
    inline float view_dir_length = 6.f;

    inline bool  skeleton = false;
    inline float skeleton_thickness = 1.4f;
    inline bool  skeleton_outline = true;

    inline float font_size = 13.f;

    inline std::uint32_t box_color{ 0xFFFFFFFFu };       // top
    inline std::uint32_t box_fill_color{ 0x28000000u };
    inline std::uint32_t box_fill_color2{ 0x50000000u };
    inline std::uint32_t name_color{ 0xFFFFFFFFu };
    inline std::uint32_t distance_color{ 0xFFFFFFFFu };
    inline std::uint32_t tool_color{ 0xFF64DC3Cu };
    inline std::uint32_t flags_color{ 0xFFFFFFFFu };
    inline std::uint32_t friendly_color{ 0xFF64DC3Cu };
    inline std::uint32_t head_dot_color{ 0xFFFFFFFFu };
    inline std::uint32_t view_dir_color{ 0xFFFFC040u };
    inline std::uint32_t skeleton_color{ 0xFFFFFFFFu };

    void draw(const std::vector<rbx::Player>& list);
    void invalidate();
}
