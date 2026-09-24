#pragma once
#include <cstdint>
#include <vector>

#include "vec.h"
#include "rbx.h"

struct ImDrawList;

// light overlay: fps / frame time / player totals (the full players list lives
// in the menu's Players tab, so this only ships the perf stats).
namespace perf
{
	inline bool stats = true;

	void render(ImDrawList* dl, const std::vector<rbx::Player>& list, const Vec2& dims);
} // namespace perf