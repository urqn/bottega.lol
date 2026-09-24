#pragma once
#include <cstdint>
#include <string>

#include "vec.h"

struct ImDrawList;

// hit detection: watches enemy health drops while the player is firing at a
// target, then plays hitsounds/killsounds, shows hit markers and toasts.
namespace hit
{
	inline bool enabled = false;

	inline bool sounds_enabled = false;
	inline int  hitsound_type = 1;   // 0 = custom path, 1..4 = built-in
	inline float hitsound_volume = 1.0f;
	inline std::string hitsound_path;

	inline bool killsound_enabled = false;
	inline int  killsound_type = 1;  // 0 = custom path, 1..4 = built-in
	inline float killsound_volume = 1.0f;
	inline std::string killsound_path;

	inline bool markers_enabled = true;
	inline float marker_lifetime = 1.3f;
	inline bool damage_text = true;
	inline bool hit_toasts = true;

	void start();
	void shutdown();
	void render(ImDrawList* dl, const Mat4& vm, const Vec2& dims);
} // namespace hit