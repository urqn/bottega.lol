#pragma once
#include <string>
#include <vector>

// triggerbot: fires a click whenever the camera ray (or crosshair) is over a
// valid enemy part. ported from the Ivory trigger with bottega.lol offsets.
namespace trigger
{
	inline bool enabled = false;

	// keybind + mode mirror the movement gate (0 hold, 1 toggle, 2 always).
	inline int key = 0;
	inline int key_mode = 0;

	inline int method = 1;       // 1 = 3D raycast, 0 = 2D crosshair distance
	inline int target_part = 0;  // see part_labels()
	inline float threshold = 6.0f;   // px (2D) / size padding factor (raycast)
	inline bool gun_check = true;
	inline float delay_ms = 0.0f;
	inline float cooldown_ms = 50.0f;

	std::vector<std::string> part_labels();

	void start();   // lazily spawns the worker thread
	void shutdown();
} // namespace trigger