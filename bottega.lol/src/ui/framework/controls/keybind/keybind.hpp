#pragma once
#include <base/animation/animation.hpp>
#include <base/object/object.hpp>
#include <functional>
#include <memory>
#include <string>

namespace gui
{
	class Keybind final : public Object
	{
	public:
		Keybind(std::string_view label_name, int init_value = 0);

		void render() override;
		void dispatch_event(Event& e) override;
		float get_preferred_width() override;

		std::string label{};
		int value{ 0 };
		int m_default_value{ 0 };
		bool awaiting{ false };

		void reset_to_default() override {
			value = m_default_value;
			if (on_change) on_change(value);
		}

		Animation hover_anim{};
		Animation active_anim{};

		std::function<void(int)> on_change{};

	private:
		bool is_hovered{ false };

		std::string key_name() const;
	};
}
