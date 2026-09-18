#include "keybind.hpp"
#include <input/input.hpp>
#include <render/render.hpp>
#include <utils/style.hpp>

#include <cstdio>

namespace gui
{
	namespace
	{
		constexpr float k_box_width = 74.0f;
		constexpr float k_box_height = 16.0f;

		std::string vk_to_name(int vk)
		{
			if (vk <= 0) return "None";

			switch (vk)
			{
			case 1:  return "Mouse 1";
			case 2:  return "Mouse 2";
			case 4:  return "Mouse 3";
			case 5:  return "Mouse 4";
			case 6:  return "Mouse 5";
			case 0x08: return "Backspace";
			case 0x09: return "Tab";
			case 0x0D: return "Enter";
			case 0x10: return "Shift";
			case 0x11: return "Ctrl";
			case 0x12: return "Alt";
			case 0x14: return "Caps lock";
			case 0x1B: return "Escape";
			case 0x20: return "Space";
			case 0x21: return "Page up";
			case 0x22: return "Page down";
			case 0x23: return "End";
			case 0x24: return "Home";
			case 0x25: return "Left";
			case 0x26: return "Up";
			case 0x27: return "Right";
			case 0x28: return "Down";
			case 0x2D: return "Insert";
			case 0x2E: return "Delete";
			case 0x5B: return "Left win";
			case 0x5C: return "Right win";
			}

			if (vk >= '0' && vk <= '9') { char b[2]{ (char)vk, 0 }; return b; }
			if (vk >= 'A' && vk <= 'Z') { char b[2]{ (char)vk, 0 }; return b; }
			if (vk >= 0x60 && vk <= 0x69) { char b[16]; sprintf_s(b, "Num %d", vk - 0x60); return b; }
			if (vk >= 0x70 && vk <= 0x87) { char b[16]; sprintf_s(b, "F%d", vk - 0x6F); return b; }

			char b[16];
			sprintf_s(b, "VK 0x%02X", vk);
			return b;
		}

		int mouse_button_vk(MouseButton button)
		{
			switch (button)
			{
			case MouseButton::Left:   return 1;
			case MouseButton::Right:  return 2;
			case MouseButton::Middle: return 4;
			case MouseButton::X1:     return 5;
			case MouseButton::X2:     return 6;
			}
			return 0;
		}
	}

	Keybind::Keybind(std::string_view label_name, int init_value)
		: Object(label_name), label(label_name), value(init_value), m_default_value(init_value)
	{
		YGNodeStyleSetWidthPercent(yoga_node, 100.0f);
		YGNodeStyleSetHeight(yoga_node, 20.0f);
		YGNodeStyleSetFlexDirection(yoga_node, YGFlexDirectionRow);
		YGNodeStyleSetAlignItems(yoga_node, YGAlignFlexStart);

		YGNodeStyleSetMargin(yoga_node, YGEdgeBottom, style::padding);
	}

	void Keybind::render()
	{
		glm::vec2 pos = get_absolute_position();
		glm::vec2 size = { get_width(), get_height() };

		hover_anim.update(is_hovered ? 1.0f : 0.0f);
		active_anim.update(awaiting ? 1.0f : 0.0f);

		render::add_text(
			render::Fonts::NotoSans16px,
			label,
			pos + glm::vec2{ 0.0f, size.y * 0.5f },
			style::colors::text_inactive.lerp(style::colors::text_active, hover_anim),
			render::TextFlagsNone,
			glm::vec2{ 0.0f, 0.5f }
		);

		const glm::vec2 box_pos = pos + glm::vec2{ size.x - k_box_width, (size.y - k_box_height) * 0.5f };
		const glm::vec2 box_size = glm::vec2{ k_box_width, k_box_height };

		const std::string text = awaiting ? std::string("press a key") : key_name();
		const glm::vec2 text_size = render::get_text_size(render::Fonts::NotoSans12px, text);

		render::add_shadow_rect(box_pos, box_size, style::colors::window_shadow, 15.0f, style::widget_rounding);
		render::add_rect_filled(box_pos, box_size, style::colors::accent.scale_alpha(active_anim), style::widget_rounding);
		render::add_rect_filled(box_pos, box_size, style::colors::control_bg.lerp(style::colors::hovered_control_bg, hover_anim).scale_alpha(1.0f - active_anim), style::widget_rounding);
		render::add_rect(box_pos, box_size, style::colors::window_border, style::widget_rounding, 2.0f);

		render::add_text(
			render::Fonts::NotoSans12px,
			text,
			box_pos + glm::vec2{ box_size.x * 0.5f - text_size.x * 0.5f, box_size.y * 0.5f },
			style::colors::text_inactive.lerp(style::colors::text_active, active_anim),
			render::TextFlagsNone,
			glm::vec2{ 0.0f, 0.5f }
		);

		Object::render();
	}

	void Keybind::dispatch_event(Event& e)
	{
		Object::dispatch_event(e);
		if (e.handled) return;

		glm::vec2 pos = get_absolute_position();
		glm::vec2 size = { get_width(), get_height() };
		glm::vec2 box_pos = pos + glm::vec2{ size.x - k_box_width, (size.y - k_box_height) * 0.5f };
		glm::vec2 box_size = { k_box_width, k_box_height };

		if (e.get_type() == EventType::MouseMove)
		{
			auto& me = static_cast<MouseMoveEvent&>(e);
			is_hovered = input::in_bounds(me.position, pos, size) || input::in_bounds(me.position, box_pos, box_size);
		}
		else if (e.get_type() == EventType::MouseButton)
		{
			auto& me = static_cast<MouseButtonEvent&>(e);

			if (me.button == MouseButton::Right && me.pressed)
			{
				// right-click anywhere on the keybind clears the binding
				if (input::in_bounds(me.position, pos, size))
				{
					value = 0;
					awaiting = false;
					if (on_change) on_change(value);
					e.handled = true;
					return;
				}
			}

			if (me.button == MouseButton::Left && me.pressed)
			{
				if (input::in_bounds(me.position, box_pos, box_size))
				{
					awaiting = !awaiting;
					e.handled = true;
					return;
				}

				awaiting = false;
				e.handled = input::in_bounds(me.position, pos, size);
				return;
			}

			if (awaiting && me.pressed)
			{
				value = mouse_button_vk(me.button);
				awaiting = false;
				if (on_change) on_change(value);
				e.handled = true;
			}
		}
		else if (e.get_type() == EventType::KeyPress)
		{
			if (!awaiting) return;

			auto& ke = static_cast<KeyPressEvent&>(e);
			if (ke.key == 0x1B) value = 0;
			else value = ke.key;

			awaiting = false;
			if (on_change) on_change(value);
			e.handled = true;
		}
		else if (e.get_type() == EventType::KeyRelease)
		{
			if (awaiting) e.handled = true;
		}
	}

	float Keybind::get_preferred_width()
	{
		glm::vec2 text_size = render::get_text_size(render::Fonts::NotoSans16px, label);
		return text_size.x + style::padding * 2.0f + k_box_width;
	}

	std::string Keybind::key_name() const
	{
		return vk_to_name(value);
	}
}
