#pragma once

// hitbox expander: scales the HRP primitive of every enemy and optionally
// disables collision, restoring original values on disable or despawn.
namespace hbe
{
	inline bool enabled = false;
	inline float scale_x = 1.5f;
	inline float scale_y = 1.5f;
	inline float scale_z = 1.5f;
	inline bool disable_collision = true;

	void start();
	void shutdown();
} // namespace hbe