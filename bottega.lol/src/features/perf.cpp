#include "perf.h"

#include <Windows.h>

#include <chrono>
#include <cstdio>
#include <string>

#include "imgui.h"

namespace perf
{
	namespace
	{
		std::chrono::steady_clock::time_point g_last;
		float g_fps = 60.0f;
	}

	void render(ImDrawList* dl, const std::vector<rbx::Player>& list, const Vec2& dims)
	{
		if (!dl || dims.x < 100.0f || dims.y < 100.0f) return;

		const auto now = std::chrono::steady_clock::now();
		if (g_last.time_since_epoch().count() != 0)
		{
			const float dt = std::chrono::duration<float>(now - g_last).count();
			if (dt > 0.0001f) g_fps = (g_fps * 0.9f) + (1.0f / dt) * 0.1f;
		}
		g_last = now;

		auto dl_text = [&](float x, float y, const char* text, ImU32 col)
		{
			dl->AddText(ImVec2{ x + 1, y + 1 }, IM_COL32(0, 0, 0, 220), text);
			dl->AddText(ImVec2{ x, y }, col, text);
		};

		if (stats)
		{
			int enemies = 0, allies = 0;
			for (const auto& p : list)
			{
				if (p.friendly) ++allies; else ++enemies;
			}

			char buf[160];
			std::snprintf(buf, sizeof(buf), "bottega.lol | %.0f fps", g_fps);
			dl_text(12.0f, 12.0f, buf, IM_COL32(200, 80, 255, 255));

			std::snprintf(buf, sizeof(buf), "players: %zu  (enemies %d / allies %d)", list.size(), enemies, allies);
			dl_text(12.0f, 26.0f, buf, IM_COL32(255, 255, 255, 255));
		}
	}
} // namespace perf