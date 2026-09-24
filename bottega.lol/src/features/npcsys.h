#pragma once
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

// NPC system config editor (ported from ivory). Each entry describes a model or
// a folder of models under Workspace plus which bot configs should drive it.
// Persisted as JSON beside the other configs so scripts can consume it.
namespace npcsys
{
	enum class EntryType : int { Model = 0, Directory = 1 };

	enum class MatchMode : int { ExactPath = 0, ContainsName = 1, ClassName = 2, RecursiveSearch = 3 };

	struct NpcEntry
	{
		std::string name = "Folder";
		std::string path = "Islands/Spawn/Parts/Rigs/R15";
		EntryType  type = EntryType::Directory;
		MatchMode  match_mode = MatchMode::ExactPath;
		bool       hostile = true;
		std::vector<std::string> configs{ "1" };
	};

	// editable buffers for the currently selected entry (UI-facing).
	inline std::mutex g_mtx;
	inline std::vector<NpcEntry> g_entries;
	inline int g_selected = 0;
	inline char g_name[128] = "Folder";
	inline char g_path[256] = "Islands/Spawn/Parts/Rigs/R15";
	inline char g_config[128] = "1";
	inline int g_selected_config = 0;

	bool has_entries();
	int  count();
	void load_into(char* name, char* path);
	void sync_current();

	void init();                 // seed defaults once
	void add_model();
	void add_dir();
	void remove_selected();

	bool is_dir(int index);
	std::string label(int index);          // "Name [Dir]"
	const char* const* match_modes();

	void save_config();
	void load_config();

	std::string config_dir();
} // namespace npcsys