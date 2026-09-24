#include "waypoints.h"
#include "movement.h"

#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "imgui.h"
#include "mem.h"
#include "offsets.h"
#include "rbx.h"

#include <nlohmann/json.hpp>

namespace wp
{
	namespace
	{
		std::vector<Waypoint> g_waypoints;
		std::mutex g_mutex;

		std::uint64_t o_place_id = 0;
		std::uint64_t o_game_id = 0;

		// resolved lazily from the published map; guarded (0 == not published).
		void ensure_offsets()
		{
			if (o_place_id != 0) return; // best-effort sentinel
			o_place_id = off::find("DataModel.PlaceId");
			o_game_id = off::find("DataModel.GameId");
		}

		std::string dir()
		{
			return "C:\\bottega\\waypoints";
		}

		bool ensure_dir()
		{
			try { std::filesystem::create_directories(dir()); return true; }
			catch (...) { return false; }
		}

		std::string ts()
		{
			const auto now = std::chrono::system_clock::now();
			const auto t = std::chrono::system_clock::to_time_t(now);
			std::tm bt{};
			localtime_s(&bt, &t);
			std::ostringstream ss;
			ss << std::put_time(&bt, "%Y-%m-%d %H:%M:%S");
			return ss.str();
		}

		std::string path_of(const std::string& name)
		{
			return dir() + "\\" + name + ".json";
		}
	} // namespace

	std::vector<Waypoint> list()
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		return g_waypoints;
	}

	bool add(const std::string& name, const Vec3& pos, const float col[4])
	{
		bool ok = false;
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			Waypoint w;
			w.name = name.empty() ? ("Waypoint " + std::to_string(g_waypoints.size() + 1)) : name;
			w.pos = pos;
			w.place_id = current_place_id();
			w.timestamp = ts();
			w.game_id = (o_game_id && rbx::datamodel) ? mem::read<std::uint64_t>(rbx::datamodel + o_game_id) : 0;
			if (col)
			{
				w.color[0] = col[0]; w.color[1] = col[1]; w.color[2] = col[2]; w.color[3] = col[3];
			}
			g_waypoints.push_back(w);
			ok = true;
		}
		if (ok && auto_save) save_current_game();
		return ok;
	}

	bool remove(size_t index)
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		if (index >= g_waypoints.size()) return false;
		g_waypoints.erase(g_waypoints.begin() + static_cast<ptrdiff_t>(index));
		return true;
	}

	void clear()
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		g_waypoints.clear();
	}

	bool teleport(size_t index)
	{
		Vec3 pos{};
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			if (index >= g_waypoints.size()) return false;
			pos = g_waypoints[index].pos;
		}
		pos.y += 1.0f; // avoid clipping into the floor
		mv::teleport_to(pos);
		return true;
	}

	std::vector<std::string> profiles()
	{
		ensure_dir();
		std::vector<std::string> out;
		try
		{
			for (const auto& entry : std::filesystem::directory_iterator(dir()))
			{
				if (entry.is_regular_file() && entry.path().extension() == ".json")
					out.push_back(entry.path().stem().string());
			}
		}
		catch (...) {}
		std::sort(out.begin(), out.end());
		return out;
	}

	bool save(const std::string& name)
	{
		if (name.empty()) return false;
		ensure_dir();
		std::lock_guard<std::mutex> lock(g_mutex);
		try
		{
			nlohmann::json arr = nlohmann::json::array();
			for (const auto& w : g_waypoints)
			{
				arr.push_back({
					{ "name", w.name },
					{ "pos", { w.pos.x, w.pos.y, w.pos.z } },
					{ "game_id", w.game_id },
					{ "place_id", w.place_id },
					{ "timestamp", w.timestamp },
					{ "color", { w.color[0], w.color[1], w.color[2], w.color[3] } },
					{ "show_tracer", w.show_tracer },
					{ "show_distance", w.show_distance },
				});
			}
			nlohmann::json root{ { "created_at", ts() }, { "waypoints", arr } };
			std::ofstream f(path_of(name));
			if (!f.is_open()) return false;
			f << root.dump(4);
			f.close();
			return true;
		}
		catch (...) { return false; }
	}

	bool load(const std::string& name)
	{
		if (name.empty()) return false;
		ensure_dir();
		const std::string p = path_of(name);
		if (!std::filesystem::exists(p)) return false;
		std::lock_guard<std::mutex> lock(g_mutex);
		try
		{
			std::ifstream f(p);
			if (!f.is_open()) return false;
			nlohmann::json root;
			f >> root;
			f.close();

			if (!root.contains("waypoints") || !root["waypoints"].is_array()) return false;

			std::vector<Waypoint> loaded;
			for (const auto& jw : root["waypoints"])
			{
				Waypoint w;
				w.name = jw.value("name", std::string("Waypoint"));
				if (jw.contains("pos") && jw["pos"].is_array() && jw["pos"].size() >= 3)
				{
					w.pos.x = jw["pos"][0].get<float>();
					w.pos.y = jw["pos"][1].get<float>();
					w.pos.z = jw["pos"][2].get<float>();
				}
				w.game_id = jw.value("game_id", 0ull);
				w.place_id = jw.value("place_id", 0ull);
				w.timestamp = jw.value("timestamp", std::string());
				if (jw.contains("color") && jw["color"].is_array() && jw["color"].size() >= 4)
				{
					for (int i = 0; i < 4; ++i) w.color[i] = jw["color"][i].get<float>();
				}
				w.show_tracer = jw.value("show_tracer", true);
				w.show_distance = jw.value("show_distance", true);
				loaded.push_back(w);
			}
			g_waypoints = std::move(loaded);
			return true;
		}
		catch (...) { return false; }
	}

	bool remove_profile(const std::string& name)
	{
		if (name.empty()) return false;
		try
		{
			if (std::filesystem::exists(path_of(name)))
				return std::filesystem::remove(path_of(name));
		}
		catch (...) {}
		return false;
	}

	std::uint64_t current_place_id()
	{
		ensure_offsets();
		if (!rbx::datamodel || !o_place_id) return 0;
		return mem::read<std::uint64_t>(rbx::datamodel + o_place_id);
	}

	bool save_current_game()
	{
		std::uint64_t id = current_place_id();
		if (id == 0) id = (o_game_id && rbx::datamodel) ? mem::read<std::uint64_t>(rbx::datamodel + o_game_id) : 0;
		if (id == 0) return false;
		return save("place_" + std::to_string(id));
	}

	bool load_current_game()
	{
		std::uint64_t id = current_place_id();
		if (id == 0) id = (o_game_id && rbx::datamodel) ? mem::read<std::uint64_t>(rbx::datamodel + o_game_id) : 0;
		if (id == 0) return false;
		return load("place_" + std::to_string(id));
	}

	void render(ImDrawList* dl, const Mat4& vm, const Vec2& dims)
	{
		if (!render_enabled || !dl || dims.x < 100.0f || dims.y < 100.0f) return;

		Vec3 cam_pos{};
		{
			const std::uintptr_t cam = rbx::fresh_camera();
			if (cam) cam_pos = mem::read<Vec3>(cam + off::CameraPos);
		}

		const std::vector<Waypoint> copy = list();
		for (const auto& w : copy)
		{
			Vec2 scr{};
			if (!rbx::w2s(w.pos, scr, vm, dims.x, dims.y)) continue;

			const ImU32 col = IM_COL32(int(w.color[0] * 255.0f), int(w.color[1] * 255.0f), int(w.color[2] * 255.0f), int(w.color[3] * 255.0f));
			const ImU32 outline = IM_COL32(0, 0, 0, 220);

			const float r = 5.0f;
			dl->AddQuadFilled(ImVec2{ scr.x, scr.y - r }, ImVec2{ scr.x + r, scr.y }, ImVec2{ scr.x, scr.y + r }, ImVec2{ scr.x - r, scr.y }, col);
			dl->AddQuad(ImVec2{ scr.x, scr.y - r }, ImVec2{ scr.x + r, scr.y }, ImVec2{ scr.x, scr.y + r }, ImVec2{ scr.x - r, scr.y }, outline, 1.0f);

			if (render_tracers && w.show_tracer)
				dl->AddLine(ImVec2{ dims.x * 0.5f, dims.y }, ImVec2{ scr.x, scr.y }, col, 1.0f);

			std::string label = w.name;
			if (render_distance && w.show_distance)
			{
				Vec3 d{ w.pos.x - cam_pos.x, w.pos.y - cam_pos.y, w.pos.z - cam_pos.z };
				const float dist = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
				label += " [" + std::to_string(static_cast<int>(dist)) + "m]";
			}

			const ImVec2 sz = ImGui::CalcTextSize(label.c_str());
			const ImVec2 tp{ scr.x - sz.x * 0.5f, scr.y + 6.0f };
			dl->AddText(ImVec2{ tp.x + 1, tp.y + 1 }, outline, label.c_str());
			dl->AddText(tp, col, label.c_str());
		}
	}

	// end anonymous namespace
} // namespace wp