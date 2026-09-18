#include "../base/events/events.hpp"
#include "../config/config.hpp"
#include "../controls/button/button.hpp"
#include "../controls/checkbox/checkbox.hpp"
#include "../controls/container/container.hpp"
#include "../controls/dropdown/dropdown.hpp"
#include "../controls/label/label.hpp"
#include "../controls/listbox/listbox.hpp"
#include "../controls/popup/popup.hpp"
#include "../controls/slider/slider.hpp"
#include "../controls/subtab_bar/subtab_bar.hpp"
#include "../controls/tab_control/tab_control.hpp"
#include "../controls/text_input/text_input.hpp"

#include <algorithm>
#include <app/app.hpp>
#include <cctype>
#include <controls/color_picker/color_picker.hpp>
#include <controls/keybind/keybind.hpp>
#include <controls/multi_dropdown/multi_dropdown.hpp>
#include <ctime>
#include <format>
#include <imgui.h>
#include <iomanip>
#include <render/assets/font_awesome.hpp>
#include <render/render.hpp>
#include <sstream>
#include <string>
#include <string_encryption.hpp>
#include <utils/style.hpp>
#include <windowsx.h>

#include "aim.h"
#include "esp.h"
#include "mem.h"
#include "movement.h"
#include "offsets.h"
#include "overlay.h"
#include "rbx.h"
#include "settings.h"

namespace app
{
	static std::vector<gui::Label*> g_info_runtime{};
	static std::vector<gui::Label*> g_info_pointers{};
	static gui::Listbox* g_player_list{ nullptr };
	static gui::TextInput* g_player_search{ nullptr };
	static std::vector<rbx::Player> g_players_snapshot{};

	static const rbx::Player* selected_player();

	static gui::Checkbox* g_watermark{ nullptr };
	static gui::Checkbox* g_vsync{ nullptr };
	static gui::Checkbox* g_streamproof{ nullptr };
	static gui::Checkbox* g_esp_enabled{ nullptr };
	static gui::Checkbox* g_esp_self{ nullptr };
	static gui::Checkbox* g_esp_box{ nullptr };
	static gui::Checkbox* g_esp_name{ nullptr };
	static gui::Slider<float>* g_esp_max_dist{ nullptr };
	static gui::Slider<float>* g_esp_font_size{ nullptr };
	static gui::Dropdown* g_esp_box_style{ nullptr };
	static gui::Dropdown* g_esp_box_type{ nullptr };
	static gui::Checkbox* g_esp_box_filled{ nullptr };
	static gui::Checkbox* g_esp_box_gradient{ nullptr };
	static gui::Checkbox* g_esp_health{ nullptr };
	static gui::Checkbox* g_esp_distance{ nullptr };
	static gui::Checkbox* g_esp_tool{ nullptr };
	static gui::Checkbox* g_esp_flags{ nullptr };
	static gui::Checkbox* g_esp_friendly{ nullptr };
	static gui::MultiDropdown* g_esp_flag_sel{ nullptr };
	static gui::Checkbox* g_esp_head_dot{ nullptr };
	static gui::Slider<float>* g_esp_head_dot_size{ nullptr };
	static gui::Checkbox* g_esp_view_dir{ nullptr };
	static gui::Slider<float>* g_esp_view_dir_len{ nullptr };
	static gui::Checkbox* g_esp_skeleton{ nullptr };
	static gui::Checkbox* g_esp_skeleton_outline{ nullptr };
	static gui::Slider<float>* g_esp_skeleton_thickness{ nullptr };
	static gui::ColorPicker* g_esp_box_color{ nullptr };
	static gui::ColorPicker* g_esp_box_fill_color{ nullptr };
	static gui::ColorPicker* g_esp_box_fill_color2{ nullptr };
	static gui::ColorPicker* g_esp_name_color{ nullptr };
	static gui::ColorPicker* g_esp_distance_color{ nullptr };
	static gui::ColorPicker* g_esp_tool_color{ nullptr };
	static gui::ColorPicker* g_esp_flags_color{ nullptr };
	static gui::ColorPicker* g_esp_friendly_color{ nullptr };
	static gui::ColorPicker* g_esp_head_dot_color{ nullptr };
	static gui::ColorPicker* g_esp_view_dir_color{ nullptr };
	static gui::ColorPicker* g_esp_skeleton_color{ nullptr };
	static gui::ColorPicker* g_watermark_color{ nullptr };

	static gui::Checkbox* g_aim_enabled{ nullptr };
	static gui::Checkbox* g_aim_prediction{ nullptr };
	static gui::Checkbox* g_aim_auto_fire{ nullptr };
	static gui::Checkbox* g_aim_draw_fov{ nullptr };
	static gui::Checkbox* g_aim_silent{ nullptr };
	static gui::Checkbox* g_aim_tracer{ nullptr };
	static gui::Checkbox* g_aim_magic{ nullptr };
	static gui::Checkbox* g_aim_draw_silent_fov{ nullptr };

	static gui::Checkbox* g_misc_team_check{ nullptr };
	static gui::Checkbox* g_misc_dead_check{ nullptr };

	static gui::Keybind* g_aim_key{ nullptr };
	static gui::Keybind* g_aim_silent_key{ nullptr };

	static gui::Dropdown* g_aim_aimpart{ nullptr };
	static gui::Dropdown* g_aim_silentpart{ nullptr };
	static gui::Dropdown* g_aim_method{ nullptr };
	static gui::Dropdown* g_aim_silent_method{ nullptr };
	static gui::Dropdown* g_aim_mode{ nullptr };
	static gui::Dropdown* g_aim_silent_mode{ nullptr };

	static gui::Slider<float>* g_aim_fov{ nullptr };
	static gui::Slider<float>* g_aim_smooth{ nullptr };
	static gui::Slider<float>* g_aim_pred_speed{ nullptr };
	static gui::Slider<float>* g_aim_auto_fire_delay{ nullptr };
	static gui::Slider<float>* g_aim_auto_fire_fov{ nullptr };
	static gui::Slider<float>* g_aim_silent_fov{ nullptr };

	static gui::ColorPicker* g_aim_fov_color{ nullptr };
	static gui::ColorPicker* g_aim_tracer_color{ nullptr };
	static gui::ColorPicker* g_aim_silent_fov_color{ nullptr };

	static gui::Checkbox* g_mv_walk{ nullptr };
	static gui::Checkbox* g_mv_jump{ nullptr };
	static gui::Checkbox* g_mv_hip{ nullptr };
	static gui::Checkbox* g_mv_gravity{ nullptr };
	static gui::Checkbox* g_mv_fov{ nullptr };
	static gui::Checkbox* g_mv_bhop{ nullptr };
	static gui::Checkbox* g_mv_noclip{ nullptr };
	static gui::Dropdown* g_mv_noclip_mode{ nullptr };
	static gui::Checkbox* g_mv_fly{ nullptr };
	static gui::Checkbox* g_mv_freecam{ nullptr };
	static gui::Checkbox* g_mv_freecam_freeze{ nullptr };

	static gui::Slider<float>* g_mv_walk_value{ nullptr };
	static gui::Slider<float>* g_mv_jump_value{ nullptr };
	static gui::Slider<float>* g_mv_hip_value{ nullptr };
	static gui::Slider<float>* g_mv_gravity_value{ nullptr };
	static gui::Slider<float>* g_mv_fov_value{ nullptr };
	static gui::Slider<float>* g_mv_bhop_value{ nullptr };
	static gui::Slider<float>* g_mv_fly_speed{ nullptr };
	static gui::Slider<float>* g_mv_fly_damp{ nullptr };
	static gui::Slider<float>* g_mv_fly_vert{ nullptr };
	static gui::Slider<float>* g_mv_fc_speed{ nullptr };
	static gui::Slider<float>* g_mv_fc_sens{ nullptr };

	static gui::Keybind* g_mv_walk_key{ nullptr };
	static gui::Keybind* g_mv_jump_key{ nullptr };
	static gui::Keybind* g_mv_hip_key{ nullptr };
	static gui::Keybind* g_mv_gravity_key{ nullptr };
	static gui::Keybind* g_mv_fov_key{ nullptr };
	static gui::Keybind* g_mv_bhop_key{ nullptr };
	static gui::Keybind* g_mv_noclip_key{ nullptr };
	static gui::Keybind* g_mv_fly_key{ nullptr };
	static gui::Keybind* g_mv_freecam_key{ nullptr };

	static gui::Dropdown* g_mv_walk_mode{ nullptr };
	static gui::Dropdown* g_mv_jump_mode{ nullptr };
	static gui::Dropdown* g_mv_hip_mode{ nullptr };
	static gui::Dropdown* g_mv_gravity_mode{ nullptr };
	static gui::Dropdown* g_mv_fov_mode{ nullptr };
	static gui::Dropdown* g_mv_bhop_mode{ nullptr };
	static gui::Dropdown* g_mv_noclip_key_mode{ nullptr };
	static gui::Dropdown* g_mv_fly_mode{ nullptr };
	static gui::Dropdown* g_mv_freecam_mode{ nullptr };

	static gui::ColorPicker* g_theme_bg{ nullptr };
	static gui::ColorPicker* g_theme_panels{ nullptr };
	static gui::ColorPicker* g_theme_controls{ nullptr };
	static gui::ColorPicker* g_theme_accent{ nullptr };
	static gui::ColorPicker* g_theme_text{ nullptr };
	static gui::ColorPicker* g_theme_text_bright{ nullptr };

	static ImU32 to_u32(const Color& c)
	{
		return IM_COL32(c.r, c.g, c.b, c.a);
	}

	static constexpr const char* k_win_label = "bottega";
	static constexpr const char* k_tab_aim = "Aim";
	static constexpr const char* k_tab_visuals = "Visuals";
	static constexpr const char* k_tab_players = "Players";
	static constexpr const char* k_tab_misc = "Misc";
	static constexpr const char* k_tab_info = "Info";
	static constexpr const char* k_grp_esp = "Player ESP";
	static constexpr const char* k_grp_screen = "Screen";

	void setup()
	{
		{
			std::lock_guard lock(gui_mutex);
			windows.clear();
		}

		auto window = std::make_shared<gui::Window>(k_win_label, glm::vec2{ 100.f, 100.f }, glm::vec2{ 620.f, 490.f });
		window->window_title_show = xs("bottega");
		window->window_tld = xs(".lol");

		if (auto tabs = window->add_object<gui::TabControl>())
		{
			if (auto aim_tab = tabs->add_tab(k_tab_aim, ICON_FA_CROSSHAIRS))
			{
				if (auto aimbot = aim_tab->add_container(xs("Aimbot")))
				{
					g_aim_enabled = aimbot->add_object<gui::Checkbox>(xs("Enabled"));
					if (auto popup = g_aim_enabled->add_object<gui::Popup>(xs("Keybind")))
					{
						g_aim_key = popup->add_object<gui::Keybind>(xs("Key"));
						g_aim_mode = popup->add_object<gui::Dropdown>(xs("Mode"), 0, std::vector<std::string>{ "Hold", "Toggle", "Always" });
					}

					g_aim_method = aimbot->add_object<gui::Dropdown>(xs("Method"), 0, std::vector<std::string>{ "Mouse", "Memory" });
					g_aim_fov = aimbot->add_object<gui::Slider<float>>(xs("FOV"), 10.0f, 800.0f, 120.0f, xs("px"), 0, 5.0f);
					g_aim_smooth = aimbot->add_object<gui::Slider<float>>(xs("Smoothness"), 1.0f, 25.0f, 4.0f, xs("x"), 1, 0.5f);
					g_aim_aimpart = aimbot->add_object<gui::Dropdown>(xs("Hit part"), 0, aim::body_part_labels());

					g_aim_prediction = aimbot->add_object<gui::Checkbox>(xs("Prediction"), false);
					if (auto pred = g_aim_prediction->add_object<gui::Popup>(xs("Prediction")))
					{
						g_aim_pred_speed = pred->add_object<gui::Slider<float>>(xs("Projectile speed"), 100.0f, 5000.0f, 1000.0f, xs("studs/s"), 0, 50.0f);
					}

					g_aim_auto_fire = aimbot->add_object<gui::Checkbox>(xs("Auto fire"), false);
					if (auto fire = g_aim_auto_fire->add_object<gui::Popup>(xs("Auto fire")))
					{
						g_aim_auto_fire_delay = fire->add_object<gui::Slider<float>>(xs("Delay"), 0.0f, 1000.0f, 50.0f, xs("ms"), 0, 5.0f);
						g_aim_auto_fire_fov = fire->add_object<gui::Slider<float>>(xs("Trigger FOV"), 1.0f, 100.0f, 6.0f, xs("px"), 0, 1.0f);
					}

					g_aim_draw_fov = aimbot->add_object<gui::Checkbox>(xs("Draw FOV"), false);
					if (auto circle = g_aim_draw_fov->add_object<gui::Popup>(xs("Draw FOV")))
					{
						auto line = circle->add_object<gui::Label>(xs("Color"));
						g_aim_fov_color = line->add_object<gui::ColorPicker>(xs("Color"), Color(242, 78, 107), false);
					}
				}

				if (auto silent_c = aim_tab->add_container(xs("Silent aim")))
				{
					g_aim_silent = silent_c->add_object<gui::Checkbox>(xs("Enabled"));
					if (auto popup = g_aim_silent->add_object<gui::Popup>(xs("Keybind")))
					{
						g_aim_silent_key = popup->add_object<gui::Keybind>(xs("Key"));
						g_aim_silent_mode = popup->add_object<gui::Dropdown>(xs("Mode"), 0, std::vector<std::string>{ "Hold", "Toggle", "Always" });
					}

					g_aim_silent_method = silent_c->add_object<gui::Dropdown>(xs("Method"), 0, std::vector<std::string>{ "Viewport", "Raycast" });

					g_aim_silentpart = silent_c->add_object<gui::Dropdown>(xs("Hit part"), 0, aim::body_part_labels());

					g_aim_magic = silent_c->add_object<gui::Checkbox>(xs("Magic bullet"), false);

					g_aim_silent_fov = silent_c->add_object<gui::Slider<float>>(xs("FOV"), 10.0f, 800.0f, 250.0f, xs("px"), 0, 5.0f);

					g_aim_draw_silent_fov = silent_c->add_object<gui::Checkbox>(xs("Draw FOV"), false);
					if (auto scircle = g_aim_draw_silent_fov->add_object<gui::Popup>(xs("Draw FOV")))
					{
						auto sline = scircle->add_object<gui::Label>(xs("Color"));
						g_aim_silent_fov_color = sline->add_object<gui::ColorPicker>(xs("Color"), Color(76, 230, 27), false);
					}

					g_aim_tracer = silent_c->add_object<gui::Checkbox>(xs("Tracer"), false);
					if (auto tracer = g_aim_tracer->add_object<gui::Popup>(xs("Tracer")))
					{
						auto tline = tracer->add_object<gui::Label>(xs("Color"));
						g_aim_tracer_color = tline->add_object<gui::ColorPicker>(xs("Color"), Color(255, 91, 240), false);
					}
				}
			}

			if (auto visuals = tabs->add_tab(k_tab_visuals, ICON_FA_EYE))
			{
				if (auto esp_c = visuals->add_container(k_grp_esp))
				{
					g_esp_enabled = esp_c->add_object<gui::Checkbox>(xs("Enabled"));

					if (auto popup = g_esp_enabled->add_object<gui::Popup>(xs("Options")))
					{
						g_esp_max_dist = popup->add_object<gui::Slider<float>>(xs("Max distance"), 100.0f, 5000.0f, 1500.0f, xs("m"), 0, 50.0f);
						g_esp_font_size = popup->add_object<gui::Slider<float>>(xs("Font size"), 8.0f, 22.0f, 13.0f, xs("px"), 1, 1.0f);
						g_esp_friendly = popup->add_object<gui::Checkbox>(xs("Friendly check"), false);
						g_esp_friendly_color = g_esp_friendly->add_object<gui::ColorPicker>(xs("Friendly"), Color(100, 220, 60));
					}

					g_esp_self = esp_c->add_object<gui::Checkbox>(xs("Self ESP"));

					g_esp_box = esp_c->add_object<gui::Checkbox>(xs("Box"));
					g_esp_box_color = g_esp_box->add_object<gui::ColorPicker>(xs("Color"), Color(242, 78, 107));
					if (auto bpop = g_esp_box->add_object<gui::Popup>(xs("Box options")))
					{
						g_esp_box_style = bpop->add_object<gui::Dropdown>(xs("Style"), 0, std::vector<std::string>{ "Static", "Dynamic" });
						g_esp_box_type = bpop->add_object<gui::Dropdown>(xs("Type"), 0, std::vector<std::string>{ "Bounding", "Corner" });
						g_esp_box_filled = bpop->add_object<gui::Checkbox>(xs("Filled"), false);
						g_esp_box_fill_color = g_esp_box_filled->add_object<gui::ColorPicker>(xs("Fill"), Color(0, 0, 0, 40), true);
						g_esp_box_gradient = bpop->add_object<gui::Checkbox>(xs("Gradient fill"), true);
						g_esp_box_fill_color2 = g_esp_box_gradient->add_object<gui::ColorPicker>(xs("Fill 2"), Color(0, 0, 0, 80), true);
					}

					g_esp_health = esp_c->add_object<gui::Checkbox>(xs("Health bar"));

					g_esp_name = esp_c->add_object<gui::Checkbox>(xs("Name"));
					g_esp_name_color = g_esp_name->add_object<gui::ColorPicker>(xs("Color"), Color::white());

					g_esp_distance = esp_c->add_object<gui::Checkbox>(xs("Distance"));
					g_esp_distance_color = g_esp_distance->add_object<gui::ColorPicker>(xs("Color"), Color::white());

					g_esp_tool = esp_c->add_object<gui::Checkbox>(xs("Tool"));
					g_esp_tool_color = g_esp_tool->add_object<gui::ColorPicker>(xs("Color"), Color(100, 220, 60));

					g_esp_flags = esp_c->add_object<gui::Checkbox>(xs("Flags"));
					g_esp_flags_color = g_esp_flags->add_object<gui::ColorPicker>(xs("Color"), Color::white());
					if (auto fpop = g_esp_flags->add_object<gui::Popup>(xs("Flags")))
					{
						g_esp_flag_sel = fpop->add_object<gui::MultiDropdown>(xs("Which"), 0x3F,
							std::vector<std::string>{ "State", "Rig", "Health", "Tool", "Distance", "Speed" });
					}

					g_esp_skeleton = esp_c->add_object<gui::Checkbox>(xs("Skeleton"));
					g_esp_skeleton_color = g_esp_skeleton->add_object<gui::ColorPicker>(xs("Color"), Color::white());
					if (auto spop = g_esp_skeleton->add_object<gui::Popup>(xs("Skeleton")))
					{
						g_esp_skeleton_thickness = spop->add_object<gui::Slider<float>>(xs("Thickness"), 0.5f, 4.0f, 1.4f, xs("px"), 1, 0.1f);
						g_esp_skeleton_outline = spop->add_object<gui::Checkbox>(xs("Outline"), true);
					}

					g_esp_head_dot = esp_c->add_object<gui::Checkbox>(xs("Head dot"));
					g_esp_head_dot_color = g_esp_head_dot->add_object<gui::ColorPicker>(xs("Color"), Color::white());
					if (auto hpop = g_esp_head_dot->add_object<gui::Popup>(xs("Head dot")))
					{
						g_esp_head_dot_size = hpop->add_object<gui::Slider<float>>(xs("Size"), 1.0f, 8.0f, 3.0f, xs("px"), 1, 0.5f);
					}

					g_esp_view_dir = esp_c->add_object<gui::Checkbox>(xs("View direction"));
					g_esp_view_dir_color = g_esp_view_dir->add_object<gui::ColorPicker>(xs("Color"), Color(255, 192, 64));
					if (auto vpop = g_esp_view_dir->add_object<gui::Popup>(xs("View direction")))
					{
						g_esp_view_dir_len = vpop->add_object<gui::Slider<float>>(xs("Length"), 1.0f, 50.0f, 6.0f, xs("u"), 0, 1.0f);
					}
				}

				if (auto screen = visuals->add_container(k_grp_screen))
				{
					g_watermark = screen->add_object<gui::Checkbox>(xs("Watermark"), false);
					g_watermark_color = g_watermark->add_object<gui::ColorPicker>(xs("Color"), gui::style::colors::accent);

					g_vsync = screen->add_object<gui::Checkbox>(xs("Vertical sync"), false);
					g_streamproof = screen->add_object<gui::Checkbox>(xs("Streamproof"), false);
				}
			}

			if (auto players = tabs->add_tab(k_tab_players, ICON_FA_USERS))
			{
				if (auto search = players->add_container(xs("Search")))
				{
					search->should_save = false;
					g_player_search = search->add_object<gui::TextInput>(xs("Search"), xs("filter by name..."));
					g_player_search->show_label = false;
				}

				if (auto list = players->add_container(xs("Online")))
				{
					list->should_save = false;
					g_player_list = list->add_object<gui::Listbox>(xs("Players"), std::vector<std::string>{}, 12);
					g_player_list->show_label = false;
				}

				if (auto actions = players->add_container(xs("Actions")))
				{
					actions->should_save = false;

					auto spectate_btn = actions->add_object<gui::Button>(xs("Spectate"));
					spectate_btn->on_press = []() {
						const rbx::Player* p = selected_player();
						if (p && p->humanoid)
							mv::spectate(p->humanoid);
						};

					auto teleport_btn = actions->add_object<gui::Button>(xs("Teleport"));
					teleport_btn->on_press = []() {
						const rbx::Player* p = selected_player();
						if (p && p->hrp)
							mv::teleport_to_player(p->hrp);
						};
				}
			}

			if (auto misc = tabs->add_tab(k_tab_misc, ICON_FA_BURGER))
			{
				if (auto configs = misc->add_container(xs("Configs")))
				{
					configs->should_save = false;

					auto config_list = configs->add_object<gui::Listbox>(xs("Config files"), std::vector<std::string>{}, 10);
					config_list->show_label = false;

					auto config_name = configs->add_object<gui::TextInput>(xs("Config name"), xs("enter new config name"));
					config_name->show_label = false;

					class CreateButton : public gui::Button {
					public:
						gui::TextInput* input;
						CreateButton(gui::TextInput* i) : gui::Button(xs("Create")), input(i) {}
						void update_layout() override {
							should_display = !input->storage.empty();
							gui::Button::update_layout();
						}
					};

					auto create_btn = configs->add_object<CreateButton>(config_name);
					create_btn->on_press = [config_list, config_name]() {
						std::string name = config_name->storage;
						if (!name.empty())
						{
							config::save_by_name(name);
							config_name->storage.clear();
							config::refresh_file_list(config_list);
						}
						};

					auto save_btn = configs->add_object<gui::Button>(xs("Save"));
					save_btn->on_press = [config_list]() {
						if (config_list->value >= 0 && config_list->value < (int)config_list->options.size())
						{
							std::string name = config_list->options[config_list->value];
							if (!name.empty())
							{
								config::save_by_name(name);
								config::refresh_file_list(config_list);
							}
						}
						};

					auto load_btn = configs->add_object<gui::Button>(xs("Load"));
					load_btn->on_press = [config_list]() {
						if (config_list->value >= 0 && config_list->value < (int)config_list->options.size())
							config::load_by_name(config_list->options[config_list->value]);
						};

					auto reset_btn = configs->add_object<gui::Button>(xs("Reset"));
					reset_btn->on_press = []() {
						config::reset();
						};

					config::refresh_file_list(config_list);
				}

				if (auto checks = misc->add_container(xs("Checks")))
				{
					g_misc_team_check = checks->add_object<gui::Checkbox>(xs("Team check"), false);
					g_misc_dead_check = checks->add_object<gui::Checkbox>(xs("Dead check"), true);
				}

				if (auto local_c = misc->add_container(xs("Local")))
				{
					g_mv_walk = local_c->add_object<gui::Checkbox>(xs("Walk speed"));
					if (auto p = g_mv_walk->add_object<gui::Popup>(xs("Walk speed")))
					{
						g_mv_walk_value = p->add_object<gui::Slider<float>>(xs("Speed"), 1.0f, 500.0f, 16.0f, xs(""), 0, 1.0f);
						g_mv_walk_key = p->add_object<gui::Keybind>(xs("Key"));
						g_mv_walk_mode = p->add_object<gui::Dropdown>(xs("Mode"), 0, std::vector<std::string>{ "Hold", "Toggle", "Always" });
					}

					g_mv_jump = local_c->add_object<gui::Checkbox>(xs("Jump power"));
					if (auto p = g_mv_jump->add_object<gui::Popup>(xs("Jump power")))
					{
						g_mv_jump_value = p->add_object<gui::Slider<float>>(xs("Power"), 1.0f, 500.0f, 50.0f, xs(""), 0, 1.0f);
						g_mv_jump_key = p->add_object<gui::Keybind>(xs("Key"));
						g_mv_jump_mode = p->add_object<gui::Dropdown>(xs("Mode"), 0, std::vector<std::string>{ "Hold", "Toggle", "Always" });
					}

					g_mv_hip = local_c->add_object<gui::Checkbox>(xs("Hip height"));
					if (auto p = g_mv_hip->add_object<gui::Popup>(xs("Hip height")))
					{
						g_mv_hip_value = p->add_object<gui::Slider<float>>(xs("Height"), 0.0f, 20.0f, 2.0f, xs(""), 1, 0.1f);
						g_mv_hip_key = p->add_object<gui::Keybind>(xs("Key"));
						g_mv_hip_mode = p->add_object<gui::Dropdown>(xs("Mode"), 0, std::vector<std::string>{ "Hold", "Toggle", "Always" });
					}

					g_mv_gravity = local_c->add_object<gui::Checkbox>(xs("Gravity"));
					if (auto p = g_mv_gravity->add_object<gui::Popup>(xs("Gravity")))
					{
						g_mv_gravity_value = p->add_object<gui::Slider<float>>(xs("Value"), 0.0f, 500.0f, 196.2f, xs(""), 1, 1.0f);
						g_mv_gravity_key = p->add_object<gui::Keybind>(xs("Key"));
						g_mv_gravity_mode = p->add_object<gui::Dropdown>(xs("Mode"), 0, std::vector<std::string>{ "Hold", "Toggle", "Always" });
					}

					g_mv_fov = local_c->add_object<gui::Checkbox>(xs("Field of view"));
					if (auto p = g_mv_fov->add_object<gui::Popup>(xs("Field of view")))
					{
						g_mv_fov_value = p->add_object<gui::Slider<float>>(xs("FOV"), 1.0f, 180.0f, 70.0f, xs(""), 0, 1.0f);
						g_mv_fov_key = p->add_object<gui::Keybind>(xs("Key"));
						g_mv_fov_mode = p->add_object<gui::Dropdown>(xs("Mode"), 0, std::vector<std::string>{ "Hold", "Toggle", "Always" });
					}
				}

				if (auto move_c = misc->add_container(xs("Movement")))
				{
					g_mv_bhop = move_c->add_object<gui::Checkbox>(xs("Bunny hop"));
					if (auto p = g_mv_bhop->add_object<gui::Popup>(xs("Bunny hop")))
					{
						g_mv_bhop_value = p->add_object<gui::Slider<float>>(xs("Speed"), 1.0f, 250.0f, 30.0f, xs(""), 0, 1.0f);
						g_mv_bhop_key = p->add_object<gui::Keybind>(xs("Key"));
						g_mv_bhop_mode = p->add_object<gui::Dropdown>(xs("Mode"), 0, std::vector<std::string>{ "Hold", "Toggle", "Always" });
					}

					g_mv_noclip = move_c->add_object<gui::Checkbox>(xs("Noclip"));
					if (auto p = g_mv_noclip->add_object<gui::Popup>(xs("Noclip")))
					{
						g_mv_noclip_mode = p->add_object<gui::Dropdown>(xs("Mode"), 0, std::vector<std::string>{ "All parts", "Root only" });
						g_mv_noclip_key = p->add_object<gui::Keybind>(xs("Key"));
						g_mv_noclip_key_mode = p->add_object<gui::Dropdown>(xs("Key mode"), 0, std::vector<std::string>{ "Hold", "Toggle", "Always" });
					}

					g_mv_fly = move_c->add_object<gui::Checkbox>(xs("Fly"));
					if (auto p = g_mv_fly->add_object<gui::Popup>(xs("Fly")))
					{
						g_mv_fly_speed = p->add_object<gui::Slider<float>>(xs("Speed"), 1.0f, 500.0f, 50.0f, xs(""), 0, 1.0f);
						g_mv_fly_damp = p->add_object<gui::Slider<float>>(xs("Damping"), 0.0f, 40.0f, 10.0f, xs(""), 0, 1.0f);
						g_mv_fly_vert = p->add_object<gui::Slider<float>>(xs("Vertical"), 0.0f, 5.0f, 1.0f, xs("x"), 1, 0.1f);
						g_mv_fly_key = p->add_object<gui::Keybind>(xs("Key"));
						g_mv_fly_mode = p->add_object<gui::Dropdown>(xs("Mode"), 0, std::vector<std::string>{ "Hold", "Toggle", "Always" });
					}

					g_mv_freecam = move_c->add_object<gui::Checkbox>(xs("Freecam"));
					if (auto p = g_mv_freecam->add_object<gui::Popup>(xs("Freecam")))
					{
						g_mv_fc_speed = p->add_object<gui::Slider<float>>(xs("Speed"), 0.1f, 20.0f, 1.5f, xs(""), 1, 0.1f);
						g_mv_fc_sens = p->add_object<gui::Slider<float>>(xs("Sensitivity"), 0.0005f, 0.02f, 0.003f, xs(""), 4, 0.0005f);
						g_mv_freecam_freeze = p->add_object<gui::Checkbox>(xs("Freeze character"), false);
						g_mv_freecam_key = p->add_object<gui::Keybind>(xs("Key"));
						g_mv_freecam_mode = p->add_object<gui::Dropdown>(xs("Mode"), 0, std::vector<std::string>{ "Hold", "Toggle", "Always" });
					}
				}

				if (auto theme = misc->add_container(xs("Theme")))
				{
					auto row = [&](const char* key, const char* text, gui::ColorPicker*& slot, const Color& init) {
						auto line = theme->add_object<gui::Label>(text);
						slot = line->add_object<gui::ColorPicker>(key, init, false);
						return slot;
					};

					row(xs("theme.background"), xs("Background"), g_theme_bg, gui::style::colors::window_bg);
					row(xs("theme.panels"), xs("Panels"), g_theme_panels, gui::style::colors::tab_bg);
					row(xs("theme.controls"), xs("Controls"), g_theme_controls, gui::style::colors::control_bg);
					row(xs("theme.accent"), xs("Accent"), g_theme_accent, gui::style::colors::accent);
					row(xs("theme.text"), xs("Text"), g_theme_text, gui::style::colors::text_inactive);
					row(xs("theme.textbright"), xs("Text Bright"), g_theme_text_bright, gui::style::colors::text_active);

					auto theme_reset = theme->add_object<gui::Button>(xs("Reset theme"));
					theme_reset->on_press = []() {
						if (g_theme_bg) g_theme_bg->value = Color(20, 20, 20);
						if (g_theme_panels) g_theme_panels->value = Color(12, 12, 12);
						if (g_theme_controls) g_theme_controls->value = Color(14, 14, 14);
						if (g_theme_accent) g_theme_accent->value = Color(255, 85, 200);
						if (g_theme_text) g_theme_text->value = Color(100, 100, 100);
						if (g_theme_text_bright) g_theme_text_bright->value = Color(255, 255, 255);
						};
				}

				if (auto creds = misc->add_container(xs("Credits")))
				{
					auto base = creds->add_object<gui::Label>(xs("bottega.lol"));
					base->should_save = false;

					auto offs = creds->add_object<gui::Label>(xs("offsets: bottega-lol-offsets.vercel.app"));
					offs->should_save = false;

					auto rend = creds->add_object<gui::Label>(xs("rendering: Direct3D 11 / Win32"));
					rend->should_save = false;
				}
			}

			if (auto info = tabs->add_tab(k_tab_info, ICON_FA_CIRCLE_INFO))
			{
				if (auto runtime = info->add_container(xs("Runtime")))
				{
					runtime->should_save = false;

					for (int i = 0; i < 3; ++i)
					{
						auto line = runtime->add_object<gui::Label>(xs("..."));
						line->should_save = false;
						g_info_runtime.push_back(line);
					}
				}

				if (auto pointers = info->add_container(xs("Pointers")))
				{
					pointers->should_save = false;

					for (int i = 0; i < 4; ++i)
					{
						auto line = pointers->add_object<gui::Label>(xs("..."));
						line->should_save = false;
						g_info_pointers.push_back(line);
					}
				}
			}
		}

		windows.push_back(window);

		config::init();
	}

	void render()
	{
		render::set_to_background();

		// note: overlay::menu_open is synced from windows[0]->m_opened in the main
		// loop, not here, so the toggle keeps working while rendering is skipped.

		const bool watermark = g_watermark ? g_watermark->value : false;
		if (watermark)
			render_watermark();

		if (g_info_runtime.size() >= 3)
		{
			g_info_runtime[0]->text = std::format("version   {}", off::version.empty() ? xs("?") : off::version);
			g_info_runtime[1]->text = std::format("offsets   {}", off::map.size());
			g_info_runtime[2]->text = std::format("pid       {}", mem::dwPid);
		}

		if (g_info_pointers.size() >= 4)
		{
			g_info_pointers[0]->text = std::format("datamodel    0x{:X}", static_cast<std::uint64_t>(rbx::datamodel));
			g_info_pointers[1]->text = std::format("localplayer  0x{:X}", static_cast<std::uint64_t>(rbx::local_player));
			g_info_pointers[2]->text = std::format("camera       0x{:X}", static_cast<std::uint64_t>(rbx::camera));
			g_info_pointers[3]->text = std::format("visualengine 0x{:X}", static_cast<std::uint64_t>(rbx::visual_eng));
		}

		for (auto& window : windows)
		{
			window->render();
		}
	}

	bool watermark_enabled()
	{
		return g_watermark ? g_watermark->value : false;
	}

	void sync_features()
	{
		esp::enabled = g_esp_enabled ? g_esp_enabled->value : true;
		esp::self = g_esp_self ? g_esp_self->value : false;
		esp::friendly = g_esp_friendly ? g_esp_friendly->value : false;
		esp::max_dist = g_esp_max_dist ? g_esp_max_dist->value : 1500.0f;
		esp::font_size = g_esp_font_size ? g_esp_font_size->value : 13.0f;

		esp::box = g_esp_box ? g_esp_box->value : true;
		esp::box_style = g_esp_box_style ? g_esp_box_style->value : 0;
		esp::box_type = g_esp_box_type ? g_esp_box_type->value : 0;
		esp::box_filled = g_esp_box_filled ? g_esp_box_filled->value : false;
		esp::box_fill_gradient = g_esp_box_gradient ? g_esp_box_gradient->value : true;

		esp::health_bar = g_esp_health ? g_esp_health->value : false;
		esp::name_tag = g_esp_name ? g_esp_name->value : true;
		esp::distance = g_esp_distance ? g_esp_distance->value : false;
		esp::tool = g_esp_tool ? g_esp_tool->value : false;
		esp::flags = g_esp_flags ? g_esp_flags->value : false;
		esp::flag_sel = g_esp_flag_sel ? g_esp_flag_sel->value : 0x3F;

		esp::head_dot = g_esp_head_dot ? g_esp_head_dot->value : false;
		esp::head_dot_size = g_esp_head_dot_size ? g_esp_head_dot_size->value : 3.0f;
		esp::view_direction = g_esp_view_dir ? g_esp_view_dir->value : false;
		esp::view_dir_length = g_esp_view_dir_len ? g_esp_view_dir_len->value : 6.0f;

		esp::skeleton = g_esp_skeleton ? g_esp_skeleton->value : false;
		esp::skeleton_thickness = g_esp_skeleton_thickness ? g_esp_skeleton_thickness->value : 1.4f;
		esp::skeleton_outline = g_esp_skeleton_outline ? g_esp_skeleton_outline->value : true;

		esp::box_color = to_u32(g_esp_box_color ? g_esp_box_color->value : Color(242, 78, 107));
		esp::box_fill_color = to_u32(g_esp_box_fill_color ? g_esp_box_fill_color->value : Color(0, 0, 0, 40));
		esp::box_fill_color2 = to_u32(g_esp_box_fill_color2 ? g_esp_box_fill_color2->value : Color(0, 0, 0, 80));
		esp::name_color = to_u32(g_esp_name_color ? g_esp_name_color->value : Color::white());
		esp::distance_color = to_u32(g_esp_distance_color ? g_esp_distance_color->value : Color::white());
		esp::tool_color = to_u32(g_esp_tool_color ? g_esp_tool_color->value : Color(100, 220, 60));
		esp::flags_color = to_u32(g_esp_flags_color ? g_esp_flags_color->value : Color::white());
		esp::friendly_color = to_u32(g_esp_friendly_color ? g_esp_friendly_color->value : Color(100, 220, 60));
		esp::head_dot_color = to_u32(g_esp_head_dot_color ? g_esp_head_dot_color->value : Color::white());
		esp::view_dir_color = to_u32(g_esp_view_dir_color ? g_esp_view_dir_color->value : Color(255, 192, 64));
		esp::skeleton_color = to_u32(g_esp_skeleton_color ? g_esp_skeleton_color->value : Color::white());

		aim::enabled = g_aim_enabled ? g_aim_enabled->value : false;
		aim::silent = g_aim_silent ? g_aim_silent->value : false;
		aim::key = g_aim_key ? g_aim_key->value : 0;
		aim::silent_key = g_aim_silent_key ? g_aim_silent_key->value : 0;
		aim::mode = g_aim_mode ? g_aim_mode->value : 0;
		aim::silent_mode = g_aim_silent_mode ? g_aim_silent_mode->value : 0;
		aim::method = g_aim_method ? g_aim_method->value : 0;
		aim::silent_method = g_aim_silent_method ? g_aim_silent_method->value : 0;
		aim::fov = g_aim_fov ? g_aim_fov->value : 120.0f;
		aim::smooth = g_aim_smooth ? g_aim_smooth->value : 4.0f;
		aim::aimpart = g_aim_aimpart ? g_aim_aimpart->value : 0;
		aim::silentpart = g_aim_silentpart ? g_aim_silentpart->value : 0;
		aim::prediction = g_aim_prediction ? g_aim_prediction->value : false;
		aim::pred_speed = g_aim_pred_speed ? g_aim_pred_speed->value : 1000.0f;
		aim::auto_fire = g_aim_auto_fire ? g_aim_auto_fire->value : false;
		aim::auto_fire_delay = g_aim_auto_fire_delay ? g_aim_auto_fire_delay->value : 50.0f;
		aim::auto_fire_fov = g_aim_auto_fire_fov ? g_aim_auto_fire_fov->value : 6.0f;
		aim::draw_fov = g_aim_draw_fov ? g_aim_draw_fov->value : false;
		aim::tracer = g_aim_tracer ? g_aim_tracer->value : false;
		aim::magic_bullet = g_aim_magic ? g_aim_magic->value : false;
		aim::silent_fov = g_aim_silent_fov ? g_aim_silent_fov->value : 120.0f;
		aim::draw_silent_fov = g_aim_draw_silent_fov ? g_aim_draw_silent_fov->value : false;
		aim::fov_color = to_u32(g_aim_fov_color ? g_aim_fov_color->value : Color(242, 78, 107));
		aim::silent_fov_color = to_u32(g_aim_silent_fov_color ? g_aim_silent_fov_color->value : Color(76, 230, 27));
		aim::tracer_color = to_u32(g_aim_tracer_color ? g_aim_tracer_color->value : Color(255, 91, 240));

		mv::walk = g_mv_walk ? g_mv_walk->value : false;
		mv::jump = g_mv_jump ? g_mv_jump->value : false;
		mv::hip = g_mv_hip ? g_mv_hip->value : false;
		mv::gravity = g_mv_gravity ? g_mv_gravity->value : false;
		mv::fov = g_mv_fov ? g_mv_fov->value : false;
		mv::bhop = g_mv_bhop ? g_mv_bhop->value : false;
		mv::noclip = g_mv_noclip ? g_mv_noclip->value : false;
		mv::fly = g_mv_fly ? g_mv_fly->value : false;
		mv::freecam = g_mv_freecam ? g_mv_freecam->value : false;

		mv::walk_value = g_mv_walk_value ? g_mv_walk_value->value : 16.0f;
		mv::jump_value = g_mv_jump_value ? g_mv_jump_value->value : 50.0f;
		mv::hip_value = g_mv_hip_value ? g_mv_hip_value->value : 2.0f;
		mv::gravity_value = g_mv_gravity_value ? g_mv_gravity_value->value : 196.2f;
		mv::fov_value = g_mv_fov_value ? g_mv_fov_value->value : 70.0f;
		mv::bhop_speed = g_mv_bhop_value ? g_mv_bhop_value->value : 30.0f;
		mv::noclip_mode = g_mv_noclip_mode ? g_mv_noclip_mode->value : 0;
		mv::fly_speed = g_mv_fly_speed ? g_mv_fly_speed->value : 50.0f;
		mv::fly_damping = g_mv_fly_damp ? g_mv_fly_damp->value : 10.0f;
		mv::fly_vertical = g_mv_fly_vert ? g_mv_fly_vert->value : 1.0f;
		mv::freecam_speed = g_mv_fc_speed ? g_mv_fc_speed->value : 1.5f;
		mv::freecam_sens = g_mv_fc_sens ? g_mv_fc_sens->value : 0.003f;
		mv::freecam_freeze = g_mv_freecam_freeze ? g_mv_freecam_freeze->value : false;

		mv::walk_key = g_mv_walk_key ? g_mv_walk_key->value : 0;
		mv::jump_key = g_mv_jump_key ? g_mv_jump_key->value : 0;
		mv::hip_key = g_mv_hip_key ? g_mv_hip_key->value : 0;
		mv::gravity_key = g_mv_gravity_key ? g_mv_gravity_key->value : 0;
		mv::fov_key = g_mv_fov_key ? g_mv_fov_key->value : 0;
		mv::bhop_key = g_mv_bhop_key ? g_mv_bhop_key->value : 0;
		mv::noclip_key = g_mv_noclip_key ? g_mv_noclip_key->value : 0;
		mv::fly_key = g_mv_fly_key ? g_mv_fly_key->value : 0;
		mv::freecam_key = g_mv_freecam_key ? g_mv_freecam_key->value : 0;

		mv::walk_mode = g_mv_walk_mode ? g_mv_walk_mode->value : 0;
		mv::jump_mode = g_mv_jump_mode ? g_mv_jump_mode->value : 0;
		mv::hip_mode = g_mv_hip_mode ? g_mv_hip_mode->value : 0;
		mv::gravity_mode = g_mv_gravity_mode ? g_mv_gravity_mode->value : 0;
		mv::fov_mode = g_mv_fov_mode ? g_mv_fov_mode->value : 0;
		mv::bhop_mode = g_mv_bhop_mode ? g_mv_bhop_mode->value : 0;
		mv::noclip_key_mode = g_mv_noclip_key_mode ? g_mv_noclip_key_mode->value : 0;
		mv::fly_mode = g_mv_fly_mode ? g_mv_fly_mode->value : 0;
		mv::freecam_mode = g_mv_freecam_mode ? g_mv_freecam_mode->value : 0;

		sv::team_check = g_misc_team_check ? g_misc_team_check->value : false;
		sv::dead_check = g_misc_dead_check ? g_misc_dead_check->value : true;

		overlay::vsync = g_vsync ? g_vsync->value : false;
		overlay::set_streamproof(g_streamproof ? g_streamproof->value : false);

		if (g_theme_bg) gui::style::colors::window_bg = g_theme_bg->value;
		if (g_theme_panels) gui::style::colors::tab_bg = g_theme_panels->value;
		if (g_theme_controls) gui::style::colors::control_bg = g_theme_controls->value;
		if (g_theme_accent) gui::style::colors::accent = g_theme_accent->value;
		if (g_theme_text) gui::style::colors::text_inactive = g_theme_text->value;
		if (g_theme_text_bright) gui::style::colors::text_active = g_theme_text_bright->value;
	}

	const rbx::Player* selected_player()
	{
		if (!g_player_list)
			return nullptr;

		const int index = g_player_list->value;
		if (index < 0 || index >= static_cast<int>(g_player_list->options.size()))
			return nullptr;

		const std::string& name = g_player_list->options[index];
		if (name == xs("no players found"))
			return nullptr;

		for (const auto& p : g_players_snapshot)
		{
			if (p.name == name)
				return &p;
		}

		return nullptr;
	}

	void set_players(const std::vector<rbx::Player>& players)
	{
		g_players_snapshot = players;

		if (!g_player_list)
			return;

		std::string query = g_player_search ? g_player_search->storage : std::string{};
		std::transform(query.begin(), query.end(), query.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

		std::vector<std::string> filtered{};
		filtered.reserve(players.size());

		for (const auto& p : players)
		{
			if (!query.empty())
			{
				std::string lowered = p.name;
				std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				if (lowered.find(query) == std::string::npos)
					continue;
			}

			filtered.push_back(p.name);
		}

		if (filtered.empty())
			filtered.push_back(xs("no players found"));

		if (filtered != g_player_list->options)
			g_player_list->refresh_options(filtered);
	}

	std::string get_pretty_time()
	{
		std::time_t t = std::time(nullptr);
		std::tm* now = std::localtime(&t);
		std::stringstream ss;

		int hour = now->tm_hour;
		std::string period = (hour >= 12) ? "pm" : "am";

		hour = (hour % 12 == 0) ? 12 : hour % 12;

		ss << hour << ":"
			<< std::setfill('0') << std::setw(2) << now->tm_min
			<< period;

		return ss.str();
	}

	void render_watermark()
	{
		const Color accent = g_watermark_color ? g_watermark_color->value : gui::style::colors::accent;
		const glm::vec2 pos = glm::vec2{ 15.0f };
		const glm::vec2 watermark_img_size = glm::vec2{ 66.0f * 2.0f, 24.0f * 1.5f };
		const glm::vec2 text_size = render::get_text_size(render::Fonts::NotoSans18px, xs("bottega"));

		static std::uintptr_t last_lp{ 0 };
		static std::string cached_uname;
		static std::string cached_display;
		if (rbx::local_player != last_lp)
		{
			last_lp = rbx::local_player;
			cached_uname = rbx::local_player ? rbx::name_of(rbx::local_player) : "";
			cached_display = (rbx::local_player && off::PlayerDisplayName)
				? mem::read_lenstr(rbx::local_player + off::PlayerDisplayName) : "";
		}

		std::string uname = cached_uname;
		const std::string& display = cached_display;

		if (uname.empty()) uname = xs("bottega");

		static std::int64_t last_sec{ 0 };
		static std::string pretty_time;
		const std::int64_t now_sec = static_cast<std::int64_t>(std::time(nullptr));
		if (now_sec != last_sec)
		{
			last_sec = now_sec;
			pretty_time = get_pretty_time();
		}

		const std::string desc = std::format("{} ({}) | {}",
			uname, display.empty() ? xs("external") : display, pretty_time);
		const glm::vec2 desc_size = render::get_text_size(render::Fonts::NotoSans18px, desc);
		const glm::vec2 icon_size = render::get_text_size(render::Fonts::Icons20px, ICON_FA_BAG_SHOPPING);

		const glm::vec2 rect_size = watermark_img_size + glm::vec2{ desc_size.x + gui::style::padding * 2.0f, 0.0f };

		render::add_shadow_rect(pos, rect_size, gui::style::colors::window_shadow, 25.0f, gui::style::watermark_rounding);

		render::add_rect_filled(pos, rect_size, gui::style::colors::tab_bg, gui::style::watermark_rounding);
		render::add_rect_gradient(pos + glm::vec2{ watermark_img_size.x - 8.0f, 0.0f }, glm::vec2{ 20.0f, rect_size.y }, render::GradientType::Horizontal, gui::style::colors::window_shadow, gui::style::colors::window_shadow.scale_alpha(0.0f));

		render::draw_list->AddImageRounded(render::watermark.get_srv(), pos, pos + watermark_img_size, { 0.0f, 0.0f }, { 1.0f, 1.0f }, Color::white(), gui::style::watermark_rounding);

		render::add_circle_shadow(pos + glm::vec2{ gui::style::padding + icon_size.x * 0.5f, watermark_img_size.y * 0.5f }, 3.0f, accent, 25.0f);
		render::rotate_vertices(0.3f, [&] {
			render::add_text(render::Fonts::Icons20px, ICON_FA_BAG_SHOPPING, pos + glm::vec2{ gui::style::padding, watermark_img_size.y * 0.5f }, accent, render::TextFlagsNone, glm::vec2{ 0.0f, 0.5f });
			});

		render::add_text(render::Fonts::NotoSans18px, xs("bottega"), pos + glm::vec2{ gui::style::padding * 2.0f + icon_size.x, watermark_img_size.y * 0.5f }, Color::white(), render::TextFlagsNone, glm::vec2{ 0.0f, 0.5f });
		render::add_text(render::Fonts::NotoSans18px, xs(".lol"), pos + glm::vec2{ gui::style::padding * 2.0f + icon_size.x + text_size.x, watermark_img_size.y * 0.5f }, accent, render::TextFlagsNone, glm::vec2{ 0.0f, 0.5f });

		render::add_text(render::Fonts::NotoSans18px, desc, pos + glm::vec2{ watermark_img_size.x + gui::style::padding, rect_size.y * 0.5f }, gui::style::colors::text_hover, render::TextFlagsNone, glm::vec2{ 0.0f, 0.5f });

		render::add_rect(pos - glm::vec2{ 1.0f }, rect_size + glm::vec2{ 2.0f }, gui::style::colors::window_border, gui::style::watermark_rounding, 2.0f);
		render::add_rect(pos - glm::vec2{ 2.0f }, rect_size + glm::vec2{ 4.0f }, gui::style::colors::window_shadow, gui::style::watermark_rounding);
	}

	bool on_wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
	{
		bool handled = false;

		auto dispatch = [&](gui::Event& e) {
			for (auto it = windows.rbegin(); it != windows.rend(); ++it) {
				(*it)->dispatch_event(e);
				if (e.handled) {
					handled = true;
					break;
				}
			}
			};

		switch (msg)
		{
		case WM_MOUSEMOVE:
		{
			gui::MouseMoveEvent e({ (float)GET_X_LPARAM(lparam), (float)GET_Y_LPARAM(lparam) });
			dispatch(e);
			break;
		}
		case WM_LBUTTONDOWN:
		case WM_LBUTTONUP:
		{
			if (msg == WM_LBUTTONDOWN) ::SetCapture(hwnd);
			else ::ReleaseCapture();

			gui::MouseButtonEvent e(gui::MouseButton::Left, msg == WM_LBUTTONDOWN, { (float)GET_X_LPARAM(lparam), (float)GET_Y_LPARAM(lparam) });
			dispatch(e);
			break;
		}
		case WM_RBUTTONDOWN:
		case WM_RBUTTONUP:
		{
			if (msg == WM_RBUTTONDOWN) ::SetCapture(hwnd);
			else ::ReleaseCapture();

			gui::MouseButtonEvent e(gui::MouseButton::Right, msg == WM_RBUTTONDOWN, { (float)GET_X_LPARAM(lparam), (float)GET_Y_LPARAM(lparam) });
			dispatch(e);
			break;
		}
		case WM_MBUTTONDOWN:
		case WM_MBUTTONUP:
		{
			if (msg == WM_MBUTTONDOWN) ::SetCapture(hwnd);
			else ::ReleaseCapture();

			gui::MouseButtonEvent e(gui::MouseButton::Middle, msg == WM_MBUTTONDOWN, { (float)GET_X_LPARAM(lparam), (float)GET_Y_LPARAM(lparam) });
			dispatch(e);
			break;
		}
		case WM_XBUTTONDOWN:
		case WM_XBUTTONUP:
		{
			gui::MouseButton btn = (GET_XBUTTON_WPARAM(wparam) == XBUTTON1) ? gui::MouseButton::X1 : gui::MouseButton::X2;
			gui::MouseButtonEvent e(btn, msg == WM_XBUTTONDOWN, { (float)GET_X_LPARAM(lparam), (float)GET_Y_LPARAM(lparam) });
			dispatch(e);
			break;
		}
		case WM_MOUSEWHEEL:
		{
			gui::MouseScrollEvent e((float)GET_WHEEL_DELTA_WPARAM(wparam) / (float)WHEEL_DELTA);
			dispatch(e);
			break;
		}
		case WM_KEYDOWN:
		case WM_SYSKEYDOWN:
		{
			gui::KeyPressEvent e(static_cast<std::int32_t>(wparam));
			dispatch(e);
			break;
		}
		case WM_KEYUP:
		case WM_SYSKEYUP:
		{
			gui::KeyReleaseEvent e(static_cast<std::int32_t>(wparam));
			dispatch(e);
			break;
		}
		case WM_CHAR:
		{
			gui::KeyCharEvent e(static_cast<std::int32_t>(wparam));
			dispatch(e);
			break;
		}
		}

		return handled;
	}

	gui::Object* find_widget_recursive(gui::Object* current, std::span<const std::string_view> paths, std::size_t depth)
	{
		if (depth >= paths.size())
			return nullptr;

		gui::Object* found_obj = nullptr;
		const auto target_id = fnv::hash(paths[depth]);

		current->for_each_logical_child([&](gui::Object* child) {
			if (found_obj) return;

			if (child->id == target_id) {
				if (depth == paths.size() - 1) {
					found_obj = child;
				}
				else {
					found_obj = find_widget_recursive(child, paths, depth + 1);
				}
			}
			});

		if (!found_obj) {
			current->for_each_logical_child([&](gui::Object* child) {
				if (found_obj) return;

				if (child->m_name.empty() ||
					child->id == fnv::hash_const("TabControl") ||
					child->id == fnv::hash_const("TabControlBody"))
				{
					if (auto inner = find_widget_recursive(child, paths, depth)) {
						found_obj = inner;
					}
				}
				});
		}

		return found_obj;
	}
}
