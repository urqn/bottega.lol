#pragma once

// local avatar cosmetics: headless, korblox legs, part recolor, and
// hat/hair removal. one worker thread applies and restores via memory.
namespace cosmetic
{
	inline bool headless = false;
	inline bool korblox = false;
	inline bool recolor = false;
	inline float recolor_head[3]       = { 1.0f, 1.0f, 1.0f };
	inline float recolor_torso[3]      = { 1.0f, 1.0f, 1.0f };
	inline float recolor_left_arm[3]   = { 1.0f, 1.0f, 1.0f };
	inline float recolor_right_arm[3]  = { 1.0f, 1.0f, 1.0f };
	inline float recolor_left_leg[3]   = { 1.0f, 1.0f, 1.0f };
	inline float recolor_right_leg[3]  = { 1.0f, 1.0f, 1.0f };
	inline bool remove_hair = false;
	inline bool remove_accessories = false;

	void start();
	void shutdown();
} // namespace cosmetic