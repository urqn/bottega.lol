#pragma once
#include <cstdint>

// freeze player (ported from ivory): while the key is active, sets the client
// FFlag TargetTimeDelayFacctorTenths to a huge negative value so the controller
// never updates the local avatar (freeze in place), restoring it when released.
namespace freeze
{
	inline bool enabled = false;
	inline int  key = 0;
	inline int  key_mode = 0; // 0 hold, 1 toggle, 2 always

	void start();
	void shutdown();
}