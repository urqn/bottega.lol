#include "npcsys.h"
#include "toast.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace npcsys
{
	using json = nlohmann::json;

	static bool g_init = false;

	std::string config_dir()
	{
		std::string dir = "C:\\bottega\\configs\\";
		std::error_code ec;
		std::filesystem::create_directories(dir, ec);
		return dir;
	}

	static void seed_default()
	{
		std::lock_guard<std::mutex> lock(g_mtx);
		if (!g_entries.empty()) return;

		NpcEntry e;
		e.name = "Folder";
		e.path = "Islands/Spawn/Parts/Rigs/R15";
		e.type = EntryType::Directory;
		e.match_mode = MatchMode::ExactPath;
		e.hostile = true;
		e.configs = { "1" };
		g_entries.push_back(e);
		g_selected = 0;
		std::snprintf(g_name, sizeof(g_name), "%s", e.name.c_str());
		std::snprintf(g_path, sizeof(g_path), "%s", e.path.c_str());
	}

	void init()
	{
		if (g_init) return;
		g_init = true;
		seed_default();
		load_config();
	}

	bool has_entries()
	{
		std::lock_guard<std::mutex> lock(g_mtx);
		return !g_entries.empty();
	}

	int count()
	{
		std::lock_guard<std::mutex> lock(g_mtx);
		return static_cast<int>(g_entries.size());
	}

	void load_into(char* name, char* path)
	{
		std::lock_guard<std::mutex> lock(g_mtx);
		const std::string n = std::string(name ? name : ""), p = std::string(path ? path : "");
		if (g_selected >= 0 && g_selected < static_cast<int>(g_entries.size()))
		{
			NpcEntry& e = g_entries[g_selected];
			if (!n.empty()) e.name = n;
			if (!p.empty()) e.path = p;
		}
	}

	void sync_current()
	{
		std::lock_guard<std::mutex> lock(g_mtx);
		if (g_selected >= 0 && g_selected < static_cast<int>(g_entries.size()))
		{
			std::snprintf(g_name, sizeof(g_name), "%s", g_entries[g_selected].name.c_str());
			std::snprintf(g_path, sizeof(g_path), "%s", g_entries[g_selected].path.c_str());
		}
	}

	void add_model()
	{
		std::lock_guard<std::mutex> lock(g_mtx);
		NpcEntry e;
		e.name = "New Model";
		e.path = "Workspace/NPCs/Model";
		e.type = EntryType::Model;
		e.match_mode = MatchMode::ExactPath;
		e.hostile = true;
		e.configs = { "1" };
		g_entries.push_back(e);
		g_selected = static_cast<int>(g_entries.size()) - 1;
		std::snprintf(g_name, sizeof(g_name), "%s", e.name.c_str());
		std::snprintf(g_path, sizeof(g_path), "%s", e.path.c_str());
	}

	void add_dir()
	{
		std::lock_guard<std::mutex> lock(g_mtx);
		NpcEntry e;
		e.name = "Folder";
		e.path = "Islands/Spawn/Parts/Rigs/R15";
		e.type = EntryType::Directory;
		e.match_mode = MatchMode::ExactPath;
		e.hostile = true;
		e.configs = { "1" };
		g_entries.push_back(e);
		g_selected = static_cast<int>(g_entries.size()) - 1;
		std::snprintf(g_name, sizeof(g_name), "%s", e.name.c_str());
		std::snprintf(g_path, sizeof(g_path), "%s", e.path.c_str());
	}

	void remove_selected()
	{
		std::lock_guard<std::mutex> lock(g_mtx);
		if (g_selected < 0 || g_selected >= static_cast<int>(g_entries.size())) return;
		g_entries.erase(g_entries.begin() + g_selected);
		if (g_selected >= static_cast<int>(g_entries.size()))
			g_selected = static_cast<int>(g_entries.size()) - 1;
		if (g_selected >= 0)
		{
			std::snprintf(g_name, sizeof(g_name), "%s", g_entries[g_selected].name.c_str());
			std::snprintf(g_path, sizeof(g_path), "%s", g_entries[g_selected].path.c_str());
		}
	}

	bool is_dir(int index)
	{
		std::lock_guard<std::mutex> lock(g_mtx);
		if (index < 0 || index >= static_cast<int>(g_entries.size())) return false;
		return g_entries[index].type == EntryType::Directory;
	}

	std::string label(int index)
	{
		std::lock_guard<std::mutex> lock(g_mtx);
		if (index < 0 || index >= static_cast<int>(g_entries.size())) return "";
		const NpcEntry& e = g_entries[index];
		std::string tag = (index == g_selected) ? " >  " : "    ";
		tag += e.name;
		tag += (e.type == EntryType::Directory) ? "  [Dir]" : "  [Model]";
		return tag;
	}

	const char* const* match_modes()
	{
		static const char* modes[] = { "Exact Path", "Contains Name", "Class Name", "Recursive Search", nullptr };
		return modes;
	}

	void save_config()
	{
		std::lock_guard<std::mutex> lock(g_mtx);
		try
		{
			json arr = json::array();
			for (const auto& e : g_entries)
			{
				json j;
				j["name"] = e.name;
				j["path"] = e.path;
				j["type"] = static_cast<int>(e.type);
				j["match_mode"] = static_cast<int>(e.match_mode);
				j["hostile"] = e.hostile;
				j["configs"] = e.configs;
				arr.push_back(j);
			}
			std::ofstream file(config_dir() + "npcs.json", std::ios::trunc);
			if (file.is_open())
			{
				file << arr.dump(4);
				file.close();
				toast::push(toast::Kind::Success, "NPC config saved");
			}
		}
		catch (const std::exception&)
		{
			toast::push(toast::Kind::Error, "NPC config save failed");
		}
	}

	void load_config()
	{
		std::lock_guard<std::mutex> lock(g_mtx);
		try
		{
			const std::string filepath = config_dir() + "npcs.json";
			if (!std::filesystem::exists(filepath)) return;

			std::ifstream file(filepath);
			if (!file.is_open()) return;
			json arr;
			file >> arr;
			file.close();

			if (!arr.is_array()) return;
			g_entries.clear();
			for (const auto& j : arr)
			{
				NpcEntry e;
				e.name = j.value("name", std::string("Folder"));
				e.path = j.value("path", std::string("Islands/Spawn/Parts/Rigs/R15"));
				e.type = static_cast<EntryType>(j.value("type", 1));
				e.match_mode = static_cast<MatchMode>(j.value("match_mode", 0));
				e.hostile = j.value("hostile", true);
				if (j.contains("configs") && j["configs"].is_array())
					e.configs = j["configs"].get<std::vector<std::string>>();
				g_entries.push_back(e);
			}
			if (!g_entries.empty())
			{
				g_selected = 0;
				std::snprintf(g_name, sizeof(g_name), "%s", g_entries[0].name.c_str());
				std::snprintf(g_path, sizeof(g_path), "%s", g_entries[0].path.c_str());
			}
			toast::push(toast::Kind::Success, "NPC config loaded");
		}
		catch (const std::exception&)
		{
			toast::push(toast::Kind::Error, "NPC config load failed");
		}
	}
} // namespace npcsys