#pragma once
#include <string>
#include <vector>

// lighting manipulation + skybox presets. skybox is write-only: applied only
// when a Sky instance already exists (no instance creation in this client).
namespace world
{
	inline bool clock_time_enabled = false;
	inline float clock_time_value = 12.0f;

	inline bool ambient_enabled = false;
	inline float ambient_color[3] = { 0.6f, 0.6f, 0.6f };
	inline float outdoor_ambient[3] = { 1.0f, 1.0f, 1.0f };

	inline bool fog_enabled = false;
	inline float fog_start = 0.0f;
	inline float fog_end = 200.0f;
	inline float fog_color[3] = { 0.40f, 0.40f, 0.40f };

	inline bool exposure_enabled = false;
	inline float exposure_value = 0.0f;

	inline bool shadows_enabled = false;
	inline bool shadows_value = true;

	inline bool skybox_enabled = false;
	inline int skybox_preset = 0; // index into skybox_presets()

	std::vector<std::string> skybox_presets();

	void start();
	void shutdown();
} // namespace world