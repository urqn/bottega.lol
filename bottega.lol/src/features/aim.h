#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "rbx.h"

namespace aim
{
	inline bool enabled{ false };
	inline int key{ 0 };
	inline int mode{ 0 };          // 0 = hold, 1 = toggle, 2 = always
	inline int method{ 0 };        // 0 = mouse, 1 = memory
	inline int silent_method{ 0 }; // 0 = viewport, 1 = raycast
	inline float fov{ 120.0f };
	inline float smooth{ 4.0f };
	inline int aimpart{ 0 };       // aimbot body part (see aim::body_part_labels)
	inline int silentpart{ 0 };    // silent aim body part (see aim::body_part_labels)
	inline bool prediction{ false };
	inline float pred_speed{ 1000.0f };
	inline bool auto_fire{ false };
	inline float auto_fire_delay{ 50.0f };
	inline float auto_fire_fov{ 6.0f };

	inline bool silent{ false };
	inline int silent_key{ 0 };
	inline int silent_mode{ 0 };
	inline bool silent_active{ false };
	inline bool magic_bullet{ false };
	inline bool tracer{ false };

	inline float silent_fov{ 120.0f };
	inline bool draw_silent_fov{ false };
	inline std::uint32_t silent_fov_color{ 0xFF4CE61B };
	inline bool draw_fov{ false };
	inline std::uint32_t fov_color{ 0xFFF24E6B };
	inline std::uint32_t tracer_color{ 0xFFF05BFF };

	inline bool active{ false };
	inline bool target_found{ false };
	inline std::string target_name;
	inline float last_fire_time{ 0.0f };

	void update(std::vector<rbx::Player>& players);
	void draw();
	void shutdown();

	std::vector<std::string> body_part_labels();
}
