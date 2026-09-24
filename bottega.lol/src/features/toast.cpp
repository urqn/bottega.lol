#include "toast.h"

#include "imgui.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <deque>
#include <iterator>
#include <mutex>
#include <string>
#include <vector>

namespace toast
{
	namespace
	{
		struct Entry
		{
			Kind kind;
			std::string text;
			double born;
			float secs;
		};

		std::mutex g_mutex;
		std::deque<Entry> g_queue;

		ImU32 kind_color(Kind k)
		{
			switch (k)
			{
			// Ivory notification accents (ImNotify hex palette).
			case Kind::Success: return IM_COL32(0x15, 0x96, 0x1e, 255);
			case Kind::Warn:    return IM_COL32(0xED, 0x9B, 0x40, 255);
			case Kind::Error:   return IM_COL32(0xD6, 0x45, 0x50, 255);
			default:            return IM_COL32(0x40, 0xA2, 0xED, 255); // Info, blue
			}
		}
	}

	void push(Kind kind, const std::string& text, float secs)
	{
		if (!enabled) return;
		std::lock_guard<std::mutex> lock(g_mutex);
		if (g_queue.size() >= 32) g_queue.pop_front();
		const double now = static_cast<double>(std::chrono::steady_clock::now().time_since_epoch().count())
			/ 1000000000.0;
		g_queue.push_back(Entry{ kind, text, now, secs > 0.0f ? secs : duration });
	}

	void push_hit(const std::string& name, float damage)
	{
		char buf[128];
		std::snprintf(buf, sizeof(buf), "Hit %s for %.0f dmg", name.c_str(), damage);
		push(Kind::Info, buf);
	}

	void push_kill(const std::string& name, const std::string& weapon)
	{
		if (weapon.empty())
			push(Kind::Success, "Killed " + name);
		else
			push(Kind::Success, "Killed " + name + " with " + weapon);
	}

	bool wants_draw()
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		return !g_queue.empty();
	}

	void clear()
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		g_queue.clear();
	}

	void draw()
	{
		std::lock_guard<std::mutex> lock(g_mutex);

		const double now = static_cast<double>(std::chrono::steady_clock::now().time_since_epoch().count())
			/ 1000000000.0;

		// prune expired entries under the lock so the queue never lingers.
		while (!g_queue.empty() && now - g_queue.front().born >= g_queue.front().secs)
			g_queue.pop_front();
		while ((int)g_queue.size() > std::max(max_on_screen, 1))
			g_queue.pop_front();

		if (g_queue.empty()) return;

		ImFont* f = ImGui::GetFont();
		if (!f) return;

		auto* dl = ImGui::GetBackgroundDrawList();
		const ImVec2 disp = ImGui::GetIO().DisplaySize;

		// Ivory ImNotify layout: stacked bottom-left, newest closest to the
		// bottom, colored left accent line + shrinking bottom progress line.
		const float font_h = 14.0f;
		const float pad_x = 10.0f;
		const float h = font_h + 16.0f;
		const float spacing = 6.0f;
		const float margin = 10.0f;
		const float bsz = 1.0f;

		const ImU32 fill = IM_COL32(16, 16, 16, 235);
		const ImU32 border_outer = IM_COL32(91, 93, 100, 235);
		const ImU32 border_inner = IM_COL32(0, 0, 0, 235);

		// iterate newest last -> bottom, stacking upward.
		size_t idx = 0;
		// compute max width so the column is stable
		float col_w = 0.0f;
		size_t n = std::min<size_t>(g_queue.size(), (size_t)std::max(max_on_screen, 1));
		std::vector<float> widths(n);
		size_t vi = 0;
		for (auto it = g_queue.rbegin(); vi < n; ++it, ++vi)
		{
			const float len = f->CalcTextSizeA(font_h, FLT_MAX, 0.0f, it->text.c_str()).x;
			widths[vi] = len + pad_x * 2.0f;
			col_w = std::max(col_w, widths[vi]);
		}

		float y = disp.y - margin;
		for (size_t i = 0; i < n; ++i)
		{
			auto it = g_queue.rbegin();
			std::advance(it, (ptrdiff_t)i);

			const float age = (float)(now - it->born);
			const float remain = std::max(0.0f, it->secs - age);
			float alpha = 1.0f;
			if (remain < 0.5f) alpha = std::clamp(remain / 0.5f, 0.0f, 1.0f);

			const float w = col_w;
			const float top = y - h;
			const ImU32 accent = kind_color(it->kind);
			const float delta = it->secs > 0.0f ? std::clamp(remain / it->secs, 0.0f, 1.0f) : 1.0f;
			const float line_right = margin + w * delta;

			auto im_col = [alpha](ImU32 c) -> ImU32
			{
				return ((ImU32)((int)(((c >> 24) & 0xFF) * alpha)) << 24) | (c & 0x00FFFFFFu);
			};

			dl->AddRectFilled(ImVec2{ margin, top }, ImVec2{ margin + w, y }, im_col(fill), 0.0f);
			dl->AddRect(ImVec2{ margin, top }, ImVec2{ margin + w, y }, im_col(border_outer), 0.0f, ImDrawFlags_None, bsz);
			dl->AddRect(ImVec2{ margin + bsz, top + bsz }, ImVec2{ margin + w - bsz, y - bsz }, im_col(border_inner), 0.0f, ImDrawFlags_None, bsz);

			dl->AddLine(
				ImVec2{ margin + bsz * 0.5f, top },
				ImVec2{ margin + bsz * 0.5f, y },
				im_col(accent), bsz * 2.0f);

			if (line_right > margin + bsz)
				dl->AddLine(
					ImVec2{ margin + bsz, y - bsz * 0.5f },
					ImVec2{ line_right, y - bsz * 0.5f },
					im_col(accent), bsz * 2.0f);

			const ImVec2 text_pos{
				margin + pad_x,
				top + (h - font_h) * 0.5f
			};
			dl->AddText(f, font_h, text_pos, IM_COL32(224, 228, 235, (int)(255.0f * alpha)), it->text.c_str());

			y -= h + spacing;
		}
	}
}