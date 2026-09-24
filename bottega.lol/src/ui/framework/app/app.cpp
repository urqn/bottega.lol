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

#include "toast.h"
#include "trigger.h"
#include "hbe.h"
#include "hit.h"
#include "cosmetic.h"
#include "world.h"
#include "waypoints.h"
#include "perf.h"
#include "serverbrowser.h"
#include "check.h"
#include "freeze.h"
#include "npcsys.h"

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
	static gui::Dropdown* g_esp_box_mode{ nullptr };
	static gui::Dropdown* g_esp_box_btype{ nullptr };
	static gui::Slider<float>* g_esp_box_thickness{ nullptr };
	static gui::Checkbox* g_esp_box_outline{ nullptr };
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
	static gui::Keybind* g_menu_key{ nullptr };

	static gui::Slider<float>* g_mv_walk_value{ nullptr };
	static gui::Slider<float>* g_mv_jump_value{ nullptr };
	static gui::Slider<float>* g_mv_hip_value{ nullptr };
	static gui::Slider<float>* g_mv_gravity_value{ nullptr };
	static gui::Slider<float>* g_mv_fov_value{ nullptr };
	static gui::Slider<float>* g_mv_bhop_value{ nullptr };
	static gui::Dropdown* g_mv_fly_flight{ nullptr };
	static gui::Slider<float>* g_mv_fc_speed{ nullptr };
	static gui::Slider<float>* g_mv_fc_sens{ nullptr };

	static gui::Keybind* g_mv_noclip_key{ nullptr };
	static gui::Keybind* g_mv_fly_key{ nullptr };
	static gui::Keybind* g_mv_freecam_key{ nullptr };

	static gui::Dropdown* g_mv_noclip_key_mode{ nullptr };
	static gui::Dropdown* g_mv_fly_mode{ nullptr };
	static gui::Dropdown* g_mv_freecam_mode{ nullptr };

	static gui::Checkbox* g_fz_enabled{ nullptr };
	static gui::Keybind* g_fz_key{ nullptr };
	static gui::Dropdown* g_fz_mode{ nullptr };

	static gui::Checkbox* g_chk_enabled{ nullptr };
	static gui::Checkbox* g_npc_open{ nullptr };

	static gui::Window* g_npc_window{ nullptr };
	static gui::Listbox* g_npc_list{ nullptr };
	static gui::TextInput* g_npc_name{ nullptr };
	static gui::TextInput* g_npc_path{ nullptr };
	static gui::Dropdown* g_npc_match{ nullptr };
	static gui::Checkbox* g_npc_hostile{ nullptr };
	static gui::Listbox* g_npc_cfg_list{ nullptr };
	static gui::TextInput* g_npc_cfg_input{ nullptr };

	static gui::ColorPicker* g_theme_bg{ nullptr };
	static gui::ColorPicker* g_theme_panels{ nullptr };
	static gui::ColorPicker* g_theme_controls{ nullptr };
	static gui::ColorPicker* g_theme_accent{ nullptr };
	static gui::ColorPicker* g_theme_text{ nullptr };
	static gui::ColorPicker* g_theme_text_bright{ nullptr };

	static gui::Checkbox* g_tr_enabled{ nullptr };
	static gui::Dropdown* g_tr_key_mode{ nullptr };
	static gui::Keybind* g_tr_key{ nullptr };
	static gui::Dropdown* g_tr_method{ nullptr };
	static gui::Dropdown* g_tr_target_part{ nullptr };
	static gui::Slider<float>* g_tr_threshold{ nullptr };
	static gui::Checkbox* g_tr_gun_check{ nullptr };
	static gui::Slider<float>* g_tr_delay{ nullptr };
	static gui::Slider<float>* g_tr_cooldown{ nullptr };

	static gui::Checkbox* g_hit_enabled{ nullptr };
	static gui::Checkbox* g_hit_sounds{ nullptr };
	static gui::Dropdown* g_hit_sound_type{ nullptr };
	static gui::Slider<float>* g_hit_sound_volume{ nullptr };
	static gui::Checkbox* g_hit_killsound{ nullptr };
	static gui::Dropdown* g_hit_kill_type{ nullptr };
	static gui::Slider<float>* g_hit_kill_volume{ nullptr };
	static gui::Checkbox* g_hit_markers{ nullptr };
	static gui::Slider<float>* g_hit_marker_lifetime{ nullptr };
	static gui::Checkbox* g_hit_damage{ nullptr };
	static gui::Checkbox* g_hit_toasts{ nullptr };

	static gui::Checkbox* g_hbe_enabled{ nullptr };
	static gui::Slider<float>* g_hbe_scale_x{ nullptr };
	static gui::Slider<float>* g_hbe_scale_y{ nullptr };
	static gui::Slider<float>* g_hbe_scale_z{ nullptr };
	static gui::Checkbox* g_hbe_disable_collision{ nullptr };

	static gui::Checkbox* g_cos_headless{ nullptr };
	static gui::Checkbox* g_cos_korblox{ nullptr };
	static gui::Checkbox* g_cos_recolor{ nullptr };
	static gui::Checkbox* g_cos_remove_hair{ nullptr };
	static gui::Checkbox* g_cos_remove_accessories{ nullptr };

	static gui::Checkbox* g_wr_clock{ nullptr };
	static gui::Slider<float>* g_wr_clock_value{ nullptr };
	static gui::Checkbox* g_wr_ambient{ nullptr };
	static gui::Slider<float>* g_wr_amb_r{ nullptr };
	static gui::Slider<float>* g_wr_amb_g{ nullptr };
	static gui::Slider<float>* g_wr_amb_b{ nullptr };
	static gui::Checkbox* g_wr_fog{ nullptr };
	static gui::Slider<float>* g_wr_fog_start{ nullptr };
	static gui::Slider<float>* g_wr_fog_end{ nullptr };
	static gui::Checkbox* g_wr_exposure{ nullptr };
	static gui::Slider<float>* g_wr_exposure_value{ nullptr };
	static gui::Checkbox* g_wr_shadows{ nullptr };
	static gui::Checkbox* g_wr_skybox{ nullptr };
	static gui::Dropdown* g_wr_skybox_preset{ nullptr };

	static gui::Checkbox* g_wp_render{ nullptr };
	static gui::Checkbox* g_wp_tracers{ nullptr };
	static gui::Checkbox* g_wp_distance{ nullptr };
	static gui::Checkbox* g_wp_autosave{ nullptr };
	static gui::TextInput* g_wp_name{ nullptr };
	static gui::Listbox* g_wp_list{ nullptr };

	static gui::Checkbox* g_pf_stats{ nullptr };

	static gui::TextInput* g_sb_place{ nullptr };
	static gui::Checkbox* g_sb_full{ nullptr };
	static gui::Dropdown* g_sb_sort{ nullptr };
	static gui::Listbox* g_sb_list{ nullptr };
	static gui::Label* g_sb_status{ nullptr };
	static gui::Checkbox* g_sb_open{ nullptr };
	static gui::Window* g_sb_window{ nullptr };
	static gui::Checkbox* g_wr_shadows_value{ nullptr };

	static void refresh_wp_listbox(gui::Listbox* box);

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

				if (auto trigger_c = aim_tab->add_container(xs("Triggerbot")))
				{
					g_tr_enabled = trigger_c->add_object<gui::Checkbox>(xs("Enabled"));
					if (auto popup = g_tr_enabled->add_object<gui::Popup>(xs("Keybind")))
					{
						g_tr_key = popup->add_object<gui::Keybind>(xs("Key"));
						g_tr_key_mode = popup->add_object<gui::Dropdown>(xs("Key mode"), 0, std::vector<std::string>{ "Hold", "Toggle", "Always" });
					}

					g_tr_method = trigger_c->add_object<gui::Dropdown>(xs("Method"), 1, std::vector<std::string>{ "2D crosshair", "3D raycast" });
					g_tr_target_part = trigger_c->add_object<gui::Dropdown>(xs("Target"), 0, trigger::part_labels());
					g_tr_threshold = trigger_c->add_object<gui::Slider<float>>(xs("Threshold"), 1.0f, 60.0f, 6.0f, xs("px"), 1, 0.5f);
					g_tr_gun_check = trigger_c->add_object<gui::Checkbox>(xs("Gun check"), true);
					g_tr_delay = trigger_c->add_object<gui::Slider<float>>(xs("Delay"), 0.0f, 1000.0f, 0.0f, xs("ms"), 0, 5.0f);
					g_tr_cooldown = trigger_c->add_object<gui::Slider<float>>(xs("Cooldown"), 0.0f, 1000.0f, 50.0f, xs("ms"), 0, 5.0f);
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
					g_esp_box_color = g_esp_box->add_object<gui::ColorPicker>(xs("Color"), Color::white());
					if (auto bpop = g_esp_box->add_object<gui::Popup>(xs("Box options")))
					{
						g_esp_box_mode = bpop->add_object<gui::Dropdown>(xs("Box mode"), 0, std::vector<std::string>{ "Bounding", "Corner", "3D" });
						g_esp_box_btype = bpop->add_object<gui::Dropdown>(xs("Bounding type"), 0, std::vector<std::string>{ "Parts", "Mesh" });
						g_esp_box_thickness = bpop->add_object<gui::Slider<float>>(xs("Thickness"), 0.5f, 4.0f, 1.0f, xs("px"), 1, 0.1f);
						g_esp_box_outline = bpop->add_object<gui::Checkbox>(xs("Outline"), true);
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

				if (auto hit_c = visuals->add_container(xs("Hit marker")))
				{
					g_hit_enabled = hit_c->add_object<gui::Checkbox>(xs("Enabled"));

					g_hit_sounds = hit_c->add_object<gui::Checkbox>(xs("Hitsound"));
					if (auto spop = g_hit_sounds->add_object<gui::Popup>(xs("Hitsound")))
					{
						g_hit_sound_type = spop->add_object<gui::Dropdown>(xs("Sound"), 1, std::vector<std::string>{ "Custom", "1", "2", "3", "4" });
						g_hit_sound_volume = spop->add_object<gui::Slider<float>>(xs("Volume"), 0.0f, 1.0f, 1.0f, xs("x"), 2, 0.05f);
					}

					g_hit_killsound = hit_c->add_object<gui::Checkbox>(xs("Kill sound"));
					if (auto kpop = g_hit_killsound->add_object<gui::Popup>(xs("Kill sound")))
					{
						g_hit_kill_type = kpop->add_object<gui::Dropdown>(xs("Sound"), 1, std::vector<std::string>{ "Custom", "1", "2", "3", "4" });
						g_hit_kill_volume = kpop->add_object<gui::Slider<float>>(xs("Volume"), 0.0f, 1.0f, 1.0f, xs("x"), 2, 0.05f);
					}

					g_hit_markers = hit_c->add_object<gui::Checkbox>(xs("Hit marker"), true);
					if (auto mpop = g_hit_markers->add_object<gui::Popup>(xs("Hit marker")))
					{
						g_hit_marker_lifetime = mpop->add_object<gui::Slider<float>>(xs("Lifetime"), 0.2f, 4.0f, 1.3f, xs("s"), 2, 0.1f);
					}

					g_hit_damage = hit_c->add_object<gui::Checkbox>(xs("Damage text"), true);
					g_hit_toasts = hit_c->add_object<gui::Checkbox>(xs("Toasts"), true);
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
					}

					g_mv_jump = local_c->add_object<gui::Checkbox>(xs("Jump power"));
					if (auto p = g_mv_jump->add_object<gui::Popup>(xs("Jump power")))
					{
						g_mv_jump_value = p->add_object<gui::Slider<float>>(xs("Power"), 1.0f, 500.0f, 50.0f, xs(""), 0, 1.0f);
					}

					g_mv_hip = local_c->add_object<gui::Checkbox>(xs("Hip height"));
					if (auto p = g_mv_hip->add_object<gui::Popup>(xs("Hip height")))
					{
						g_mv_hip_value = p->add_object<gui::Slider<float>>(xs("Height"), 0.0f, 20.0f, 2.0f, xs(""), 1, 0.1f);
					}

					g_mv_gravity = local_c->add_object<gui::Checkbox>(xs("Gravity"));
					if (auto p = g_mv_gravity->add_object<gui::Popup>(xs("Gravity")))
					{
						g_mv_gravity_value = p->add_object<gui::Slider<float>>(xs("Value"), 0.0f, 500.0f, 196.2f, xs(""), 1, 1.0f);
					}

					g_mv_fov = local_c->add_object<gui::Checkbox>(xs("Field of view"));
					if (auto p = g_mv_fov->add_object<gui::Popup>(xs("Field of view")))
					{
						g_mv_fov_value = p->add_object<gui::Slider<float>>(xs("FOV"), 1.0f, 180.0f, 70.0f, xs(""), 0, 1.0f);
					}

					g_menu_key = local_c->add_object<gui::Keybind>(xs("Menu key"), VK_INSERT);
				}

				if (auto move_c = misc->add_container(xs("Movement")))
				{
					g_mv_bhop = move_c->add_object<gui::Checkbox>(xs("Bunny hop"));
					if (auto p = g_mv_bhop->add_object<gui::Popup>(xs("Bunny hop")))
					{
						g_mv_bhop_value = p->add_object<gui::Slider<float>>(xs("Speed"), 1.0f, 250.0f, 30.0f, xs(""), 0, 1.0f);
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
						g_mv_fly_flight = p->add_object<gui::Dropdown>(xs("Flight"), 1, std::vector<std::string>{ "Velocity", "Position", "Position + Rotation" });
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

					g_fz_enabled = move_c->add_object<gui::Checkbox>(xs("Freeze player"));
					if (auto p = g_fz_enabled->add_object<gui::Popup>(xs("Freeze player")))
					{
						g_fz_key = p->add_object<gui::Keybind>(xs("Key"));
						g_fz_mode = p->add_object<gui::Dropdown>(xs("Key mode"), 0, std::vector<std::string>{ "Hold", "Toggle", "Always" });
					}
				}

				if (auto hbe_c = misc->add_container(xs("Hitbox expander")))
				{
					g_hbe_enabled = hbe_c->add_object<gui::Checkbox>(xs("Enabled"));
					if (auto p = g_hbe_enabled->add_object<gui::Popup>(xs("Hitbox expander")))
					{
						g_hbe_scale_x = p->add_object<gui::Slider<float>>(xs("Scale X"), 0.75f, 5.0f, 1.5f, xs("x"), 2, 0.05f);
						g_hbe_scale_y = p->add_object<gui::Slider<float>>(xs("Scale Y"), 0.75f, 5.0f, 1.5f, xs("x"), 2, 0.05f);
						g_hbe_scale_z = p->add_object<gui::Slider<float>>(xs("Scale Z"), 0.75f, 5.0f, 1.5f, xs("x"), 2, 0.05f);
						g_hbe_disable_collision = p->add_object<gui::Checkbox>(xs("Disable collision"), true);
					}
				}

				if (auto cos_c = misc->add_container(xs("Cosmetics")))
				{
					g_cos_headless = cos_c->add_object<gui::Checkbox>(xs("Headless"));
					g_cos_korblox = cos_c->add_object<gui::Checkbox>(xs("Korblox legs"));
					g_cos_recolor = cos_c->add_object<gui::Checkbox>(xs("Recolor"));
					g_cos_remove_hair = cos_c->add_object<gui::Checkbox>(xs("Remove hair"));
					g_cos_remove_accessories = cos_c->add_object<gui::Checkbox>(xs("Remove accessories"));
				}

				if (auto world_c = misc->add_container(xs("World")))
				{
					g_wr_clock = world_c->add_object<gui::Checkbox>(xs("Clock time"));
					if (auto p = g_wr_clock->add_object<gui::Popup>(xs("Clock time")))
					{
						g_wr_clock_value = p->add_object<gui::Slider<float>>(xs("Time"), 0.0f, 24.0f, 12.0f, xs("h"), 1, 0.25f);
					}

					g_wr_ambient = world_c->add_object<gui::Checkbox>(xs("Ambient"));
					if (auto p = g_wr_ambient->add_object<gui::Popup>(xs("Ambient")))
					{
						g_wr_amb_r = p->add_object<gui::Slider<float>>(xs("R"), 0.0f, 1.0f, 0.6f, xs(""), 2, 0.01f);
						g_wr_amb_g = p->add_object<gui::Slider<float>>(xs("G"), 0.0f, 1.0f, 0.6f, xs(""), 2, 0.01f);
						g_wr_amb_b = p->add_object<gui::Slider<float>>(xs("B"), 0.0f, 1.0f, 0.6f, xs(""), 2, 0.01f);
					}

					g_wr_fog = world_c->add_object<gui::Checkbox>(xs("Fog"));
					if (auto p = g_wr_fog->add_object<gui::Popup>(xs("Fog")))
					{
						g_wr_fog_start = p->add_object<gui::Slider<float>>(xs("Start"), 0.0f, 1000.0f, 0.0f, xs(""), 0, 1.0f);
						g_wr_fog_end = p->add_object<gui::Slider<float>>(xs("End"), 0.0f, 2500.0f, 200.0f, xs(""), 0, 5.0f);
					}

					g_wr_exposure = world_c->add_object<gui::Checkbox>(xs("Exposure"));
					if (auto p = g_wr_exposure->add_object<gui::Popup>(xs("Exposure")))
					{
						g_wr_exposure_value = p->add_object<gui::Slider<float>>(xs("Value"), -5.0f, 5.0f, 0.0f, xs(""), 2, 0.05f);
					}

					g_wr_shadows = world_c->add_object<gui::Checkbox>(xs("Global shadows"));
					if (auto p = g_wr_shadows->add_object<gui::Popup>(xs("Global shadows")))
					{
						g_wr_shadows_value = p->add_object<gui::Checkbox>(xs("Enabled"), true);
					}

					g_wr_skybox = world_c->add_object<gui::Checkbox>(xs("Skybox"));
					if (auto p = g_wr_skybox->add_object<gui::Popup>(xs("Skybox")))
					{
						g_wr_skybox_preset = p->add_object<gui::Dropdown>(xs("Preset"), 0, world::skybox_presets());
					}
				}

				if (auto wp_c = misc->add_container(xs("Waypoints")))
				{
					wp_c->should_save = false;

					g_wp_render = wp_c->add_object<gui::Checkbox>(xs("Draw"), true);
					if (auto p = g_wp_render->add_object<gui::Popup>(xs("Draw")))
					{
						g_wp_tracers = p->add_object<gui::Checkbox>(xs("Tracers"), true);
						g_wp_distance = p->add_object<gui::Checkbox>(xs("Distance"), true);
					}

					g_wp_autosave = wp_c->add_object<gui::Checkbox>(xs("Per-game autosave"), true);

					g_wp_name = wp_c->add_object<gui::TextInput>(xs("Name"), xs("waypoint name"));

					auto mark = wp_c->add_object<gui::Button>(xs("Mark"));
					mark->on_press = []() {
						Vec3 pos{ 0.0f, 0.0f, 0.0f };
						for (const auto& p : g_players_snapshot)
						{
							if (p.addr == rbx::local_player && p.hrp)
							{
								pos = p.pos;
								break;
							}
						}
						if (pos.x == 0.0f && pos.y == 0.0f && pos.z == 0.0f) return;
						std::string nm = g_wp_name ? g_wp_name->storage : std::string{};
						if (nm.empty()) nm = xs("wp");
						const float col[4] = { 1.0f, 0.35f, 0.62f, 1.0f };
						wp::add(nm, pos, col);
						refresh_wp_listbox(g_wp_list);
						};

					g_wp_list = wp_c->add_object<gui::Listbox>(xs("Waypoints"), std::vector<std::string>{}, 8);
					refresh_wp_listbox(g_wp_list);

					auto tele_btn = wp_c->add_object<gui::Button>(xs("Teleport"));
					tele_btn->on_press = []() {
						if (g_wp_list && g_wp_list->value >= 0 && g_wp_list->value < (int)g_wp_list->options.size())
							wp::teleport((size_t)g_wp_list->value);
						};

					auto del_btn = wp_c->add_object<gui::Button>(xs("Delete"));
					del_btn->on_press = []() {
						if (g_wp_list && g_wp_list->value >= 0 && g_wp_list->value < (int)g_wp_list->options.size())
						{
							wp::remove((size_t)g_wp_list->value);
							refresh_wp_listbox(g_wp_list);
						}
						};

					auto clear_btn = wp_c->add_object<gui::Button>(xs("Clear"));
					clear_btn->on_press = []() {
						wp::clear();
						refresh_wp_listbox(g_wp_list);
						};
				}

				if (auto pf_c = misc->add_container(xs("Overlay")))
				{
					g_pf_stats = pf_c->add_object<gui::Checkbox>(xs("Stats"), true);
				}

				if (auto ut_c = misc->add_container(xs("Utility")))
				{
					g_chk_enabled = ut_c->add_object<gui::Checkbox>(xs("Typing check"), false);
					g_npc_open = ut_c->add_object<gui::Checkbox>(xs("NPC system"), false);
				}

				if (auto sb_c = misc->add_container(xs("Server browser")))
				{
					g_sb_open = sb_c->add_object<gui::Checkbox>(xs("Open"), false);
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
						if (g_theme_accent) g_theme_accent->value = Color(255, 255, 255);
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

		auto sb_window = std::make_shared<gui::Window>(xs("servers"), glm::vec2{ 760.f, 60.f }, glm::vec2{ 500.f, 690.f });
		sb_window->window_title_show = xs("server");
		sb_window->window_tld = xs("browser");
		sb_window->react_to_menu_key = false;
		sb_window->m_opened = false;

		if (auto query = sb_window->add_object<gui::Container>(xs("Query")))
		{
			query->should_save = false;
			YGNodeStyleSetMargin(query->yoga_node, YGEdgeTop, gui::style::tab_header_height + gui::style::padding);

			g_sb_place = query->add_object<gui::TextInput>(xs("Place ID"), xs("0"));
			g_sb_place->should_save = false;
			if (rbx::datamodel)
			{
				const uintptr_t o_place = off::find("DataModel.PlaceId");
				if (o_place)
				{
					const uint64_t cur = mem::read<uint64_t>(rbx::datamodel + o_place);
					if (cur)
					{
						servers::set_place(cur);
						g_sb_place->storage = std::string(servers::input_place_id);
					}
				}
			}

			g_sb_full = query->add_object<gui::Checkbox>(xs("Hide full"), true);
			g_sb_full->should_save = false;
			g_sb_sort = query->add_object<gui::Dropdown>(xs("Sort"), 0,
				std::vector<std::string>{ "Lowest players", "Highest players", "Lowest ping", "Highest fps" });
			g_sb_sort->should_save = false;

			auto ref_btn = query->add_object<gui::Button>(xs("Refresh"));
			ref_btn->on_press = []() {
				servers::refresh();
				};

			auto prev_btn = query->add_object<gui::Button>(xs("Prev page"));
			prev_btn->on_press = []() { servers::prev_page(); };

			auto next_btn = query->add_object<gui::Button>(xs("Next page"));
			next_btn->on_press = []() { servers::next_page(); };
		}

		if (auto srv = sb_window->add_object<gui::Container>(xs("Servers")))
		{
			srv->should_save = false;

			g_sb_list = srv->add_object<gui::Listbox>(xs("Servers"), std::vector<std::string>{}, 8);
			g_sb_list->should_save = false;
			g_sb_status = srv->add_object<gui::Label>(xs("idle"));
			g_sb_status->should_save = false;

			auto join_btn = srv->add_object<gui::Button>(xs("Join"));
			join_btn->on_press = []() {
				const std::vector<servers::Server> vis = servers::visible();
				const int i = g_sb_list ? g_sb_list->value : -1;
				if (i >= 0 && i < static_cast<int>(vis.size())) servers::join(vis[i]);
				};

			auto copy_btn = srv->add_object<gui::Button>(xs("Copy job id"));
			copy_btn->on_press = []() {
				const std::vector<servers::Server> vis = servers::visible();
				const int i = g_sb_list ? g_sb_list->value : -1;
				if (i >= 0 && i < static_cast<int>(vis.size())) servers::copy_job_id(vis[i]);
				};

			auto tmpl_btn = srv->add_object<gui::Button>(xs("Copy tscript"));
			tmpl_btn->on_press = []() {
				const std::vector<servers::Server> vis = servers::visible();
				const int i = g_sb_list ? g_sb_list->value : -1;
				if (i >= 0 && i < static_cast<int>(vis.size())) servers::copy_teleport_script(vis[i]);
				};
		}

		windows.push_back(sb_window);
		g_sb_window = sb_window.get();
		{
			auto spacer = sb_window->add_object<gui::Object>(gui::ConstName("spacer"));
			YGNodeStyleSetHeight(spacer->yoga_node, gui::style::footer_height);
			spacer->should_save = false;
		}

		auto npc_window = std::make_shared<gui::Window>(xs("npcs"), glm::vec2{ 760.f, 60.f }, glm::vec2{ 500.f, 690.f });
		npc_window->window_title_show = xs("npc");
		npc_window->window_tld = xs("system");
		npc_window->react_to_menu_key = false;
		npc_window->m_opened = false;

		npcsys::init();

		if (auto entry_c = npc_window->add_object<gui::Container>(xs("Entries")))
		{
			entry_c->should_save = false;
			YGNodeStyleSetMargin(entry_c->yoga_node, YGEdgeTop, gui::style::tab_header_height + gui::style::padding);

			g_npc_list = entry_c->add_object<gui::Listbox>(xs("Entries"), std::vector<std::string>{}, 6);
			g_npc_list->should_save = false;

			auto add_model_btn = entry_c->add_object<gui::Button>(xs("Add model"));
			add_model_btn->on_press = []() { npcsys::add_model(); };

			auto add_dir_btn = entry_c->add_object<gui::Button>(xs("Add dir"));
			add_dir_btn->on_press = []() { npcsys::add_dir(); };

			auto del_btn = entry_c->add_object<gui::Button>(xs("Delete"));
			del_btn->on_press = []() { npcsys::remove_selected(); };
		}

		if (auto edit_c = npc_window->add_object<gui::Container>(xs("Entry")))
		{
			edit_c->should_save = false;

			g_npc_name = edit_c->add_object<gui::TextInput>(xs("Name"), xs("Folder"));
			g_npc_name->should_save = false;

			g_npc_path = edit_c->add_object<gui::TextInput>(xs("Path"), xs("Islands/Spawn/Parts/Rigs/R15"));
			g_npc_path->should_save = false;

			std::vector<std::string> modes;
			for (const char* const* m = npcsys::match_modes(); *m; ++m) modes.push_back(*m);
			g_npc_match = edit_c->add_object<gui::Dropdown>(xs("Match"), 0, modes);
			g_npc_match->should_save = false;

			g_npc_hostile = edit_c->add_object<gui::Checkbox>(xs("Hostile"), true);
			g_npc_hostile->should_save = false;
		}

		if (auto cfg_c = npc_window->add_object<gui::Container>(xs("Configs")))
		{
			cfg_c->should_save = false;

			g_npc_cfg_list = cfg_c->add_object<gui::Listbox>(xs("Configs"), std::vector<std::string>{}, 3);
			g_npc_cfg_list->should_save = false;

			g_npc_cfg_input = cfg_c->add_object<gui::TextInput>(xs("Config"), xs("1"));
			g_npc_cfg_input->should_save = false;

			auto cfg_add_btn = cfg_c->add_object<gui::Button>(xs("Add config"));
			cfg_add_btn->on_press = []() {
				std::lock_guard<std::mutex> lock(npcsys::g_mtx);
				if (npcsys::g_selected >= 0 && npcsys::g_selected < static_cast<int>(npcsys::g_entries.size()))
				{
					const std::string c = g_npc_cfg_input && !g_npc_cfg_input->storage.empty()
						? g_npc_cfg_input->storage : std::string("1");
					npcsys::g_entries[npcsys::g_selected].configs.push_back(c);
					npcsys::g_selected_config = static_cast<int>(npcsys::g_entries[npcsys::g_selected].configs.size()) - 1;
				}
				};

			auto cfg_rm_btn = cfg_c->add_object<gui::Button>(xs("Remove config"));
			cfg_rm_btn->on_press = []() {
				std::lock_guard<std::mutex> lock(npcsys::g_mtx);
				if (npcsys::g_selected >= 0 && npcsys::g_selected < static_cast<int>(npcsys::g_entries.size()))
				{
					auto& cfgs = npcsys::g_entries[npcsys::g_selected].configs;
					const int i = g_npc_cfg_list ? g_npc_cfg_list->value : -1;
					if (i >= 0 && i < static_cast<int>(cfgs.size()))
						cfgs.erase(cfgs.begin() + i);
				}
				};
		}

		if (auto file_c = npc_window->add_object<gui::Container>(xs("File")))
		{
			file_c->should_save = false;

			auto save_btn = file_c->add_object<gui::Button>(xs("Save"));
			save_btn->on_press = []() { npcsys::save_config(); };

			auto load_btn = file_c->add_object<gui::Button>(xs("Load"));
			load_btn->on_press = []() {
				npcsys::load_config();
				if (g_npc_name) g_npc_name->storage = std::string(npcsys::g_name);
				if (g_npc_path) g_npc_path->storage = std::string(npcsys::g_path);
				};
		}

		windows.push_back(npc_window);
		g_npc_window = npc_window.get();
		{
			auto spacer = npc_window->add_object<gui::Object>(gui::ConstName("spacer"));
			YGNodeStyleSetHeight(spacer->yoga_node, gui::style::footer_height);
			spacer->should_save = false;
		}

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

	static void refresh_wp_listbox(gui::Listbox* box)
	{
		if (!box) return;
		const auto wps = wp::list();
		std::vector<std::string> names;
		names.reserve(wps.size());
		for (const auto& w : wps)
			names.push_back(w.name);
		box->refresh_options(names);
	}

	void sync_features()
	{
		esp::enabled = g_esp_enabled ? g_esp_enabled->value : true;
		esp::self = g_esp_self ? g_esp_self->value : false;
		esp::friendly = g_esp_friendly ? g_esp_friendly->value : false;
		esp::max_dist = g_esp_max_dist ? g_esp_max_dist->value : 1500.0f;
		esp::font_size = g_esp_font_size ? g_esp_font_size->value : 13.0f;

		esp::box = g_esp_box ? g_esp_box->value : true;
		esp::box_mode = g_esp_box_mode ? g_esp_box_mode->value : 0;
		esp::bounding_type = g_esp_box_btype ? g_esp_box_btype->value : 0;
		esp::box_thickness = g_esp_box_thickness ? g_esp_box_thickness->value : 1.0f;
		esp::box_outline = g_esp_box_outline ? g_esp_box_outline->value : true;
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

		overlay::menu_key = g_menu_key ? g_menu_key->value : 0;

		esp::box_color = to_u32(g_esp_box_color ? g_esp_box_color->value : Color::white());
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
		mv::fly_flight = g_mv_fly_flight ? g_mv_fly_flight->value : 1;
		mv::freecam_speed = g_mv_fc_speed ? g_mv_fc_speed->value : 1.5f;
		mv::freecam_sens = g_mv_fc_sens ? g_mv_fc_sens->value : 0.003f;
		mv::freecam_freeze = g_mv_freecam_freeze ? g_mv_freecam_freeze->value : false;

		mv::noclip_key = g_mv_noclip_key ? g_mv_noclip_key->value : 0;
		mv::fly_key = g_mv_fly_key ? g_mv_fly_key->value : 0;
		mv::freecam_key = g_mv_freecam_key ? g_mv_freecam_key->value : 0;

		mv::noclip_key_mode = g_mv_noclip_key_mode ? g_mv_noclip_key_mode->value : 0;
		mv::fly_mode = g_mv_fly_mode ? g_mv_fly_mode->value : 0;
		mv::freecam_mode = g_mv_freecam_mode ? g_mv_freecam_mode->value : 0;

		freeze::enabled = g_fz_enabled ? g_fz_enabled->value : false;
		freeze::key = g_fz_key ? g_fz_key->value : 0;
		freeze::key_mode = g_fz_mode ? g_fz_mode->value : 0;

		check::enabled = g_chk_enabled ? g_chk_enabled->value : false;

		sv::team_check = g_misc_team_check ? g_misc_team_check->value : false;
		sv::dead_check = g_misc_dead_check ? g_misc_dead_check->value : true;

		trigger::enabled = g_tr_enabled ? g_tr_enabled->value : false;
		trigger::key = g_tr_key ? g_tr_key->value : 0;
		trigger::key_mode = g_tr_key_mode ? g_tr_key_mode->value : 0;
		trigger::method = g_tr_method ? g_tr_method->value : 1;
		trigger::target_part = g_tr_target_part ? g_tr_target_part->value : 0;
		trigger::threshold = g_tr_threshold ? g_tr_threshold->value : 6.0f;
		trigger::gun_check = g_tr_gun_check ? g_tr_gun_check->value : true;
		trigger::delay_ms = g_tr_delay ? g_tr_delay->value : 0.0f;
		trigger::cooldown_ms = g_tr_cooldown ? g_tr_cooldown->value : 50.0f;

		hbe::enabled = g_hbe_enabled ? g_hbe_enabled->value : false;
		hbe::scale_x = g_hbe_scale_x ? g_hbe_scale_x->value : 1.5f;
		hbe::scale_y = g_hbe_scale_y ? g_hbe_scale_y->value : 1.5f;
		hbe::scale_z = g_hbe_scale_z ? g_hbe_scale_z->value : 1.5f;
		hbe::disable_collision = g_hbe_disable_collision ? g_hbe_disable_collision->value : true;

		hit::enabled = g_hit_enabled ? g_hit_enabled->value : false;
		hit::sounds_enabled = g_hit_sounds ? g_hit_sounds->value : false;
		hit::hitsound_type = g_hit_sound_type ? g_hit_sound_type->value : 1;
		hit::hitsound_volume = g_hit_sound_volume ? g_hit_sound_volume->value : 1.0f;
		hit::killsound_enabled = g_hit_killsound ? g_hit_killsound->value : false;
		hit::killsound_type = g_hit_kill_type ? g_hit_kill_type->value : 1;
		hit::killsound_volume = g_hit_kill_volume ? g_hit_kill_volume->value : 1.0f;
		hit::markers_enabled = g_hit_markers ? g_hit_markers->value : true;
		hit::marker_lifetime = g_hit_marker_lifetime ? g_hit_marker_lifetime->value : 1.3f;
		hit::damage_text = g_hit_damage ? g_hit_damage->value : true;
		hit::hit_toasts = g_hit_toasts ? g_hit_toasts->value : true;

		cosmetic::headless = g_cos_headless ? g_cos_headless->value : false;
		cosmetic::korblox = g_cos_korblox ? g_cos_korblox->value : false;
		cosmetic::recolor = g_cos_recolor ? g_cos_recolor->value : false;
		cosmetic::remove_hair = g_cos_remove_hair ? g_cos_remove_hair->value : false;
		cosmetic::remove_accessories = g_cos_remove_accessories ? g_cos_remove_accessories->value : false;

		world::clock_time_enabled = g_wr_clock ? g_wr_clock->value : false;
		world::clock_time_value = g_wr_clock_value ? g_wr_clock_value->value : 12.0f;
		world::ambient_enabled = g_wr_ambient ? g_wr_ambient->value : false;
		world::ambient_color[0] = g_wr_amb_r ? g_wr_amb_r->value : 0.6f;
		world::ambient_color[1] = g_wr_amb_g ? g_wr_amb_g->value : 0.6f;
		world::ambient_color[2] = g_wr_amb_b ? g_wr_amb_b->value : 0.6f;
		world::fog_enabled = g_wr_fog ? g_wr_fog->value : false;
		world::fog_start = g_wr_fog_start ? g_wr_fog_start->value : 0.0f;
		world::fog_end = g_wr_fog_end ? g_wr_fog_end->value : 200.0f;
		world::exposure_enabled = g_wr_exposure ? g_wr_exposure->value : false;
		world::exposure_value = g_wr_exposure_value ? g_wr_exposure_value->value : 0.0f;
		world::shadows_enabled = g_wr_shadows ? g_wr_shadows->value : false;
		world::shadows_value = g_wr_shadows_value ? g_wr_shadows_value->value : true;
		world::skybox_enabled = g_wr_skybox ? g_wr_skybox->value : false;
		world::skybox_preset = g_wr_skybox_preset ? g_wr_skybox_preset->value : 0;

		wp::render_enabled = g_wp_render ? g_wp_render->value : true;
		wp::render_tracers = g_wp_tracers ? g_wp_tracers->value : true;
		wp::render_distance = g_wp_distance ? g_wp_distance->value : true;
		if (g_wp_autosave) wp::auto_save = g_wp_autosave->value;

		perf::stats = g_pf_stats ? g_pf_stats->value : true;

		servers::exclude_full = g_sb_full ? g_sb_full->value : true;
		if (g_sb_sort) servers::sort_mode = g_sb_sort->value;

		// The datamodel is not resolved until the scan thread has run, so the
		// place id is filled in lazily here instead of trusting setup() time.
		if (servers::place_id == 0 && rbx::datamodel)
		{
			const uintptr_t o_place = off::find("DataModel.PlaceId");
			if (o_place)
			{
				const uint64_t cur = mem::read<uint64_t>(rbx::datamodel + o_place);
				if (cur)
				{
					servers::set_place(cur);
					if (g_sb_place)
						g_sb_place->storage = std::string(servers::input_place_id);
				}
			}
		}

		// checkbox drives the separate server browser window, which auto-refreshes
		// once each time it is opened.
		const bool sb_open = g_sb_open && g_sb_open->value;
		if (g_sb_window && g_sb_window->m_opened != sb_open)
			g_sb_window->m_opened = sb_open;

		static std::uint64_t g_sb_refreshed_place = 0;
		if (sb_open)
		{
			if (g_sb_refreshed_place != servers::place_id)
			{
				g_sb_refreshed_place = servers::place_id;
				if (servers::place_id != 0)
					servers::refresh();
			}
		}
		else
		{
			g_sb_refreshed_place = 0;
		}

		// keep the server listbox + status label in sync with the fetch thread
		if (g_sb_list)
		{
			static std::vector<servers::Server> g_last_sb;
			const std::vector<servers::Server> vis = servers::visible();
			bool changed = vis.size() != g_last_sb.size();
			if (!changed)
			{
				for (size_t i = 0; i < vis.size() && !changed; ++i)
					changed = vis[i].id != g_last_sb[i].id || vis[i].playing != g_last_sb[i].playing
						|| vis[i].max_players != g_last_sb[i].max_players || vis[i].ping != g_last_sb[i].ping
						|| vis[i].fps != g_last_sb[i].fps;
			}
			if (changed)
			{
				g_last_sb = vis;
				std::vector<std::string> opts;
				opts.reserve(vis.size());
				for (const auto& s : vis) opts.push_back(servers::row_label(s));
				if (opts.empty()) opts.push_back(xs("no servers found"));
				g_sb_list->refresh_options(opts);
			}
			if (g_sb_status)
				g_sb_status->text = servers::status();
		}

		// the NPC system window is driven by its own Misc toggle.
		static std::vector<std::string> g_last_npc_labels;
		static int g_last_npc_sel = -1;
		const bool npc_open = g_npc_open && g_npc_open->value;
		if (g_npc_window && g_npc_window->m_opened != npc_open)
			g_npc_window->m_opened = npc_open;

		if (npc_open && g_npc_window && g_npc_window->m_opened && g_npc_list)
		{
			npcsys::init();

			std::vector<std::string> npc_labels;
			npc_labels.reserve(static_cast<size_t>(npcsys::count()));
			for (int i = 0; i < npcsys::count(); ++i)
				npc_labels.push_back(npcsys::label(i));
			bool npc_changed = npc_labels.size() != g_last_npc_labels.size();
			if (!npc_changed)
			{
				for (size_t i = 0; i < npc_labels.size() && !npc_changed; ++i)
					npc_changed = npc_labels[i] != g_last_npc_labels[i];
			}
			if (npc_changed)
			{
				g_last_npc_labels = npc_labels;
				if (npc_labels.empty()) npc_labels.push_back(xs("no entries"));
				g_npc_list->refresh_options(npc_labels);
			}

			const int sel = g_npc_list->value;
			if (sel >= 0 && sel < npcsys::count())
			{
				if (g_last_npc_sel != sel)
				{
					g_last_npc_sel = sel;
					npcsys::g_selected = sel;
					npcsys::sync_current();
					if (g_npc_name) g_npc_name->storage = std::string(npcsys::g_name);
					if (g_npc_path) g_npc_path->storage = std::string(npcsys::g_path);
				}
				else
				{
					if (g_npc_name)
						npcsys::load_into(const_cast<char*>(g_npc_name->storage.c_str()), nullptr);
					if (g_npc_path)
						npcsys::load_into(nullptr, const_cast<char*>(g_npc_path->storage.c_str()));
				}
			}
			else
			{
				g_last_npc_sel = -1;
			}

			// push the match/hostile/type controls into the selected entry and
			// keep the config listbox in sync.
			std::vector<std::string> npc_cfgs;
			{
				std::lock_guard<std::mutex> lock(npcsys::g_mtx);
				if (npcsys::g_selected >= 0 && npcsys::g_selected < static_cast<int>(npcsys::g_entries.size()))
				{
					auto& e = npcsys::g_entries[npcsys::g_selected];
					if (g_npc_match) e.match_mode = static_cast<npcsys::MatchMode>(g_npc_match->value);
					if (g_npc_hostile) e.hostile = g_npc_hostile->value;
					npc_cfgs = e.configs;
				}
			}

			if (g_npc_cfg_list)
			{
				static std::vector<std::string> g_last_npc_cfgs;
				bool cfgs_changed = npc_cfgs.size() != g_last_npc_cfgs.size();
				if (!cfgs_changed)
				{
					for (size_t i = 0; i < npc_cfgs.size() && !cfgs_changed; ++i)
						cfgs_changed = npc_cfgs[i] != g_last_npc_cfgs[i];
				}
				if (cfgs_changed)
				{
					g_last_npc_cfgs = npc_cfgs;
					if (npc_cfgs.empty()) npc_cfgs.push_back(xs("no configs"));
					g_npc_cfg_list->refresh_options(npc_cfgs);
				}
			}
		}
		else
		{
			g_last_npc_labels.clear();
		}

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
