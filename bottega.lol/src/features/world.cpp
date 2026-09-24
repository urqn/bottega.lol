#include "world.h"

#include <Windows.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "mem.h"
#include "offsets.h"
#include "rbx.h"

namespace world
{
	namespace
	{
		struct Preset
		{
			const char* name;
			const char* up;
			const char* bk;
			const char* ft;
			const char* lf;
			const char* rt;
			const char* dn;
		};

		const Preset kPresets[] = {
			{ "Pink 1",          "rbxassetid://12635309703", "rbxassetid://12635311686", "rbxassetid://12635312870", "rbxassetid://12635313718", "rbxassetid://12635315817", "rbxassetid://12635316856" },
			{ "Candy",           "rbxassetid://103994796436499", "rbxassetid://88135141884296", "rbxassetid://71705651078185", "rbxassetid://83560072752341", "rbxassetid://96879039628172", "rbxassetid://131043401069407" },
			{ "Cakeup Night",    "rbxassetid://15983968922", "rbxassetid://15983966825", "rbxassetid://15983965025", "rbxassetid://15983967420", "rbxassetid://15983966246", "rbxassetid://15983964246" },
			{ "Space",           "rbxassetid://12064107", "rbxassetid://12064152", "rbxassetid://12064121", "rbxassetid://12063984", "rbxassetid://12064115", "rbxassetid://12064131" },
			{ "Pink Sky",        "rbxassetid://271042516", "rbxassetid://271077243", "rbxassetid://271042556", "rbxassetid://271042310", "rbxassetid://271042467", "rbxassetid://271077958" },
			{ "Minecraft",       "rbxassetid://1876545003", "rbxassetid://1876544331", "rbxassetid://1876542941", "rbxassetid://1876543392", "rbxassetid://1876543764", "rbxassetid://1876544642" },
			{ "Night Cloudy",    "rbxassetid://116758234", "rbxassetid://116758314", "rbxassetid://116758367", "rbxassetid://116758446", "rbxassetid://116758478", "rbxassetid://116758496" },
			{ "Sparkling Night", "rbxassetid://1233158420", "rbxassetid://1233158838", "rbxassetid://1233157105", "rbxassetid://1233157640", "rbxassetid://1233157995", "rbxassetid://1233159158" },
			{ "Nebula",          "rbxassetid://95020137072033", "rbxassetid://92862258103959", "rbxassetid://107665368823185", "rbxassetid://126542804346203", "rbxassetid://103716549795832", "rbxassetid://131036626982613" },
			{ "Space 2",         "rbxassetid://11336725935", "rbxassetid://11336722286", "rbxassetid://11336720418", "rbxassetid://11336725016", "rbxassetid://11336726887", "rbxassetid://11336723758" },
			{ "Pink Classic",    "rbxassetid://678556371", "rbxassetid://678556361", "rbxassetid://678556368", "rbxassetid://678556373", "rbxassetid://678556360", "rbxassetid://678556362" },
			{ "Snowy",           "http://www.roblox.com/asset/?id=155657655", "http://www.roblox.com/asset/?id=155674246", "http://www.roblox.com/asset/?id=155657609", "http://www.roblox.com/asset/?id=155657671", "http://www.roblox.com/asset/?id=155657619", "http://www.roblox.com/asset/?id=155674931" },
			{ "Milky Way",       "http://www.roblox.com/asset/?id=168387023", "http://www.roblox.com/asset/?id=168387089", "http://www.roblox.com/asset/?id=168387054", "http://www.roblox.com/asset/?id=168534432", "http://www.roblox.com/asset/?id=168387190", "http://www.roblox.com/asset/?id=168387135" },
			{ "Ghostly Night",   "http://www.roblox.com/asset/?id=248431616", "http://www.roblox.com/asset/?id=248431677", "http://www.roblox.com/asset/?id=248431598", "http://www.roblox.com/asset/?id=248431686", "http://www.roblox.com/asset/?id=248431611", "http://www.roblox.com/asset/?id=248431605" },
			{ "Piss",            "rbxassetid://2651432901", "rbxassetid://2651434974", "rbxassetid://2651435990", "rbxassetid://2651436494", "rbxassetid://2651436979", "rbxassetid://2651437350" },
			{ "Peach",           "rbxassetid://566616113", "rbxassetid://566616232", "rbxassetid://566616141", "rbxassetid://566616044", "rbxassetid://566616082", "rbxassetid://566616187" },
			{ "Retro",           "rbxassetid://18164881924", "rbxassetid://18166113875", "rbxassetid://18164870251", "rbxassetid://18164877945", "rbxassetid://18164873920", "rbxassetid://18164890128" },
			{ "Sea",             "http://www.roblox.com/asset/?id=321846018", "http://www.roblox.com/asset/?id=321846104", "http://www.roblox.com/asset/?id=321845951", "http://www.roblox.com/asset/?id=321846162", "http://www.roblox.com/asset/?id=321846207", "http://www.roblox.com/asset/?id=321846070" },
			{ "Dark",            "rbxassetid://15470149279", "rbxassetid://15470151245", "rbxassetid://15470153860", "rbxassetid://15470155938", "rbxassetid://15470158022", "rbxassetid://15470160563" },
			{ "Beach",           "http://www.roblox.com/asset/?id=151165214", "http://www.roblox.com/asset/?id=151165197", "http://www.roblox.com/asset/?id=151165224", "http://www.roblox.com/asset/?id=151165191", "http://www.roblox.com/asset/?id=151165206", "http://www.roblox.com/asset/?id=151165227" },
			{ "Rainbow",         "rbxassetid://12877085497", "rbxassetid://12877086914", "rbxassetid://12877085497", "rbxassetid://12877085497", "rbxassetid://12877085497", "rbxassetid://12877083856" },
			{ "Forest",          "http://www.roblox.com/asset/?id=237593887", "http://www.roblox.com/asset/?id=237593849", "http://www.roblox.com/asset/?id=237593922", "http://www.roblox.com/asset/?id=237593861", "http://www.roblox.com/asset/?id=237593835", "http://www.roblox.com/asset/?id=237593929" },
			{ "Lava",            "http://www.roblox.com/asset/?id=4776124334", "http://www.roblox.com/asset/?id=4776125375", "http://www.roblox.com/asset/?id=4776131365", "http://www.roblox.com/asset/?id=4776128425", "http://www.roblox.com/asset/?id=4776133150", "http://www.roblox.com/asset/?id=4776130793" },
			{ "Rainy",           "http://www.roblox.com/asset/?id=4495864450", "http://www.roblox.com/asset/?id=4495864887", "http://www.roblox.com/asset/?id=4495865458", "http://www.roblox.com/asset/?id=4495866035", "http://www.roblox.com/asset/?id=4495866584", "http://www.roblox.com/asset/?id=4495867486" },
			{ "Green",           "rbxassetid://566611187", "rbxassetid://566613198", "rbxassetid://566611142", "rbxassetid://566611266", "rbxassetid://566611300", "rbxassetid://566611218" },
			{ "Volcanic",        "http://www.roblox.com/asset/?id=150281446", "http://www.roblox.com/asset/?id=150281418", "http://www.roblox.com/asset/?id=150281461", "http://www.roblox.com/asset/?id=150281400", "http://www.roblox.com/asset/?id=150281426", "http://www.roblox.com/asset/?id=150281471" },
			{ "Lucid",           "rbxassetid://8508098796", "rbxassetid://8508103588", "rbxassetid://8508104949", "rbxassetid://8508107681", "rbxassetid://8508111092", "rbxassetid://8508112781" },
			{ "Bikini Bottom",   "http://www.roblox.com/asset/?id=7633178166", "http://www.roblox.com/asset/?id=7633178166", "http://www.roblox.com/asset/?id=7633178166", "http://www.roblox.com/asset/?id=7633178166", "http://www.roblox.com/asset/?id=7633178166", "http://www.roblox.com/asset/?id=7633178166" },
		};

		std::thread g_thread;
		std::atomic<bool> g_running{ false };

		uintptr_t o_ambient = 0, o_outdoor = 0, o_top = 0, o_bottom = 0, o_gtop = 0, o_gbottom = 0;
		uintptr_t o_fog_start = 0, o_fog_end = 0, o_fog_color = 0;
		uintptr_t o_exposure = 0, o_shadows = 0, o_clock = 0;
		uintptr_t o_sky = 0, o_skip_find = 0;
		uintptr_t o_skybox_up = 0, o_skybox_bk = 0, o_skybox_ft = 0, o_skybox_lf = 0, o_skybox_rt = 0, o_skybox_dn = 0;

		bool sky_setup = false;
		bool sky_present = false;

		struct Capture
		{
			bool captured = false;
			uintptr_t light = 0;
			Vec3 ambient, outdoor, gtop, gbottom;
			float fog_start, fog_end;
			Vec3 fog_color;
			float exposure;
			bool shadows;
		} g_cap;

		void capture(uintptr_t light)
		{
			if (g_cap.captured && g_cap.light == light) return;
			g_cap = Capture{};
			g_cap.light = light;
			if (o_ambient) g_cap.ambient = mem::read<Vec3>(light + o_ambient);
			if (o_outdoor) g_cap.outdoor = mem::read<Vec3>(light + o_outdoor);
			if (o_gtop) g_cap.gtop = mem::read<Vec3>(light + o_gtop);
			if (o_gbottom) g_cap.gbottom = mem::read<Vec3>(light + o_gbottom);
			if (o_fog_start) g_cap.fog_start = mem::read<float>(light + o_fog_start);
			if (o_fog_end) g_cap.fog_end = mem::read<float>(light + o_fog_end);
			if (o_fog_color) g_cap.fog_color = mem::read<Vec3>(light + o_fog_color);
			if (o_exposure) g_cap.exposure = mem::read<float>(light + o_exposure);
			if (o_shadows) g_cap.shadows = mem::read<bool>(light + o_shadows);
			g_cap.captured = true;
		}

		uintptr_t find_lighting()
		{
			if (!rbx::datamodel) return 0;
			return rbx::find_child_byclass(rbx::datamodel, "Lighting");
		}

		void apply_sky(uintptr_t light)
		{
			if (!o_sky) return;
			uintptr_t sky = mem::read<uintptr_t>(light + o_sky);
			if (sky) sky_present = rbx::classname(sky) == "Sky";
			if (!sky_present)
				sky_present = rbx::find_child_byclass(light, "Sky") != 0;

			if (!sky_present || !skybox_enabled) return; // write-only: no creation

			if (!(o_skybox_up && o_skybox_bk && o_skybox_ft && o_skybox_lf && o_skybox_rt && o_skybox_dn)) return;

			int idx = skybox_preset < 0 ? 0 : skybox_preset;
			const int count = static_cast<int>(sizeof(kPresets) / sizeof(kPresets[0]));
			if (idx >= count) idx = 0;
			const Preset& p = kPresets[idx];

			mem::write_string(sky + o_skybox_up, p.up);
			mem::write_string(sky + o_skybox_bk, p.bk);
			mem::write_string(sky + o_skybox_ft, p.ft);
			mem::write_string(sky + o_skybox_lf, p.lf);
			mem::write_string(sky + o_skybox_rt, p.rt);
			mem::write_string(sky + o_skybox_dn, p.dn);
		}

		void tick()
		{
			const uintptr_t light = find_lighting();
			if (!light) return;

			const bool any = clock_time_enabled || ambient_enabled || fog_enabled || exposure_enabled
				|| shadows_enabled || skybox_enabled;
			if (!any)
			{
				if (g_cap.captured && g_cap.light == light)
				{
					if (o_ambient) mem::write<Vec3>(light + o_ambient, g_cap.ambient);
					if (o_outdoor) mem::write<Vec3>(light + o_outdoor, g_cap.outdoor);
					if (o_top) mem::write<Vec3>(light + o_top, g_cap.outdoor);
					if (o_bottom) mem::write<Vec3>(light + o_bottom, g_cap.ambient);
					if (!clock_time_enabled)
					{
						if (o_gtop) mem::write<Vec3>(light + o_gtop, g_cap.gtop);
						if (o_gbottom) mem::write<Vec3>(light + o_gbottom, g_cap.gbottom);
					}
					if (o_fog_start) mem::write<float>(light + o_fog_start, g_cap.fog_start);
					if (o_fog_end) mem::write<float>(light + o_fog_end, g_cap.fog_end);
					if (o_fog_color) mem::write<Vec3>(light + o_fog_color, g_cap.fog_color);
					if (o_exposure) mem::write<float>(light + o_exposure, g_cap.exposure);
					if (o_shadows) mem::write<bool>(light + o_shadows, g_cap.shadows);
					g_cap = Capture{};
				}
				return;
			}

			if (!g_cap.captured || g_cap.light != light)
				capture(light);

			if (clock_time_enabled && o_clock)
				mem::write<float>(light + o_clock, clock_time_value);

			if (ambient_enabled)
			{
				const Vec3 amb{ ambient_color[0], ambient_color[1], ambient_color[2] };
				const Vec3 out{ outdoor_ambient[0], outdoor_ambient[1], outdoor_ambient[2] };
				if (o_ambient) mem::write<Vec3>(light + o_ambient, amb);
				if (o_outdoor) mem::write<Vec3>(light + o_outdoor, out);
				if (o_top) mem::write<Vec3>(light + o_top, out);
				if (o_bottom) mem::write<Vec3>(light + o_bottom, amb);
				if (o_gtop) mem::write<Vec3>(light + o_gtop, out);
				if (o_gbottom) mem::write<Vec3>(light + o_gbottom, amb);
			}

			if (fog_enabled)
			{
				if (o_fog_start) mem::write<float>(light + o_fog_start, fog_start);
				if (o_fog_end) mem::write<float>(light + o_fog_end, fog_end);
				if (o_fog_color) mem::write<Vec3>(light + o_fog_color, Vec3{ fog_color[0], fog_color[1], fog_color[2] });
			}

			if (exposure_enabled && o_exposure)
				mem::write<float>(light + o_exposure, exposure_value);

			if (shadows_enabled && o_shadows)
				mem::write<bool>(light + o_shadows, shadows_value);

			(void)o_skip_find;
			if (skybox_enabled)
				apply_sky(light);
		}

		void run()
		{
			while (g_running)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
				tick();
			}
		}
	} // namespace

	std::vector<std::string> skybox_presets()
	{
		std::vector<std::string> out;
		for (const auto& p : kPresets) out.emplace_back(p.name);
		return out;
	}

	void start()
	{
		if (g_running) return;
		o_ambient = off::find("Lighting.Ambient");
		o_outdoor = off::find("Lighting.OutdoorAmbient");
		o_top = off::find("Lighting.ColorShift_Top");
		o_bottom = off::find("Lighting.ColorShift_Bottom");
		o_gtop = off::find("Lighting.GradientTop");
		o_gbottom = off::find("Lighting.GradientBottom");
		o_fog_start = off::find("Lighting.FogStart");
		o_fog_end = off::find("Lighting.FogEnd");
		o_fog_color = off::find("Lighting.FogColor");
		o_exposure = off::find("Lighting.ExposureCompensation");
		o_shadows = off::find("Lighting.GlobalShadows");
		o_clock = off::find("Lighting.ClockTime");
		o_sky = off::find("Lighting.Sky");
		o_skybox_up = off::find("Sky.SkyboxUp");
		o_skybox_bk = off::find("Sky.SkyboxBk");
		o_skybox_ft = off::find("Sky.SkyboxFt");
		o_skybox_lf = off::find("Sky.SkyboxLf");
		o_skybox_rt = off::find("Sky.SkyboxRt");
		o_skybox_dn = off::find("Sky.SkyboxDn");
		g_running = true;
		g_thread = std::thread(run);
	}

	void shutdown()
	{
		g_running = false;
		if (g_thread.joinable()) g_thread.join();
		tick(); // restore
	}
} // namespace world