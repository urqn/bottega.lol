#pragma once
#include <cstdint>
#include <string>
#include <vector>

// games.roblox.com server list for the current (or typed) place, joinable via
// the roblox:// experience URI.
namespace servers
{
	struct Server
	{
		std::string id;
		int max_players = 0;
		int playing = 0;
		int ping = 0;
		float fps = 0.0f;
	};

	inline std::uint64_t place_id = 0;
	inline char input_place_id[32] = {};

	inline bool exclude_full = true;
	inline int sort_mode = 0; // 0 lowest players, 1 highest players, 2 lowest ping, 3 highest fps

	std::vector<Server> filtered();      // every server on the loaded page
	std::vector<Server> visible();       // filtered + sorted for the listbox
	std::string row_label(const Server& s);
	std::string status();
	bool is_loading();

	void set_place(std::uint64_t place);
	void refresh();            // uses place_id
	void next_page();
	void prev_page();

	void join(const Server& s);
	void copy_job_id(const Server& s);
	void copy_teleport_script(const Server& s);
} // namespace servers