#pragma once
#include <atomic>
#include <cstdint>

// typing check (ported from ivory): while the in-game chat input bar is open we
// expose `find_active()` so keybind-driven features can release their keys and
// stop fighting the chat box (no more mid-typing aim flings or walk toggles).
namespace check
{
	inline bool enabled = false;

	inline std::atomic<bool> textchatopen{ false };

	void start();
	void shutdown();

	// true while typing is detected AND the toggle is on.
	bool blocked();
}