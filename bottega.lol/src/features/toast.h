#pragma once
#include <cstdint>
#include <string>

namespace toast
{
	enum class Kind : int { Info = 0, Success, Warn, Error };

	// UI-facing settings (synced from app.cpp).
	inline bool enabled = true;
	inline int  max_on_screen = 5;
	inline float duration = 4.0f;

	void push(Kind kind, const std::string& text, float secs = -1.0f);
	void push_hit(const std::string& name, float damage);
	void push_kill(const std::string& name, const std::string& weapon = "");

	bool wants_draw();
	void draw();
	void clear();
}