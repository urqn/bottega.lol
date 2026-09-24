#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "vec.h"

struct ImDrawList;

namespace wp
{
	struct Waypoint
	{
		std::string name;
		Vec3 pos;
		std::uint64_t game_id = 0;
		std::uint64_t place_id = 0;
		std::string timestamp;
		float color[4] = { 1.0f, 0.35f, 0.62f, 1.0f };
		bool show_tracer = true;
		bool show_distance = true;
	};

	inline bool render_enabled = true;
	inline bool render_tracers = true;
	inline bool render_distance = true;
	inline bool auto_save = true;

	std::vector<Waypoint> list();          // copy under lock
	bool add(const std::string& name, const Vec3& pos, const float col[4]);
	bool remove(size_t index);
	void clear();
	bool teleport(size_t index);           // via mv::teleport_to

	std::vector<std::string> profiles();
	bool save(const std::string& name);
	bool load(const std::string& name);
	bool remove_profile(const std::string& name);

	std::uint64_t current_place_id();
	bool save_current_game();              // place_<id>.json
	bool load_current_game();

	void render(ImDrawList* dl, const Mat4& vm, const Vec2& dims);
} // namespace wp