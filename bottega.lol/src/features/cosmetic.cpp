#include "cosmetic.h"

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "mem.h"
#include "offsets.h"
#include "rbx.h"
#include "toast.h"

namespace cosmetic
{
	namespace
	{
		std::thread g_thread;
		std::atomic<bool> g_running{ false };

		uintptr_t o_transparency = 0;
		uintptr_t o_mesh_id = 0;
		uintptr_t o_color = 0;
		uintptr_t o_parent = 0;

		const char* kKorbloxMesh = "https://assetdelivery.roblox.com/v1/asset/?id=9598310133";

		struct ColorBackup
		{
			uintptr_t addr;
			Vec3 color;
		};

		struct RemovedItem
		{
			uintptr_t addr;
			bool is_hair;
		};

		std::vector<ColorBackup> g_colors;
		std::vector<RemovedItem> g_removed;

		bool was_headless = false;
		bool was_korblox = false;
		bool was_recolor = false;
		std::string original_mesh;
		Vec3 original_head{ 0.0f, 0.0f, 0.0f };

		std::string lower(std::string s)
		{
			for (auto& c : s) c = static_cast<char>(std::tolower(c));
			return s;
		}

		uintptr_t local_character()
		{
			if (!rbx::local_player) return 0;
			return mem::read<uintptr_t>(rbx::local_player + off::ModelInstance);
		}

		uintptr_t prim_of(uintptr_t part)
		{
			if (!part || !off::Primitive) return 0;
			return mem::read<uintptr_t>(part + off::Primitive);
		}

		Vec3 target_color(const std::string& name)
		{
			const std::string n = lower(name);
			if (n == "head") return { recolor_head[0], recolor_head[1], recolor_head[2] };
			if (n.find("torso") != std::string::npos || n.find("body") != std::string::npos)
				return { recolor_torso[0], recolor_torso[1], recolor_torso[2] };
			if (n.find("arm") != std::string::npos || n.find("hand") != std::string::npos)
			{
				if (n.find("left") != std::string::npos) return { recolor_left_arm[0], recolor_left_arm[1], recolor_left_arm[2] };
				return { recolor_right_arm[0], recolor_right_arm[1], recolor_right_arm[2] };
			}
			if (n.find("leg") != std::string::npos || n.find("foot") != std::string::npos)
			{
				if (n.find("left") != std::string::npos) return { recolor_left_leg[0], recolor_left_leg[1], recolor_left_leg[2] };
				return { recolor_right_leg[0], recolor_right_leg[1], recolor_right_leg[2] };
			}
			return { 1.0f, 1.0f, 1.0f };
		}

		bool recolorable(const std::string& name)
		{
			const std::string n = lower(name);
			return n == "head" || n.find("torso") != std::string::npos || n.find("body") != std::string::npos ||
				n.find("arm") != std::string::npos || n.find("hand") != std::string::npos ||
				n.find("leg") != std::string::npos || n.find("foot") != std::string::npos;
		}

		bool is_hair(uintptr_t acc)
		{
			const std::string nm = lower(rbx::name_of(acc));
			if (nm.find("hair") != std::string::npos) return true;
			for (const uintptr_t c : rbx::children(acc))
			{
				if (c && rbx::classname(c) == "Attachment" && rbx::name_of(c) == "HairAttachment")
					return true;
			}
			return false;
		}

		void apply_headless(uintptr_t character)
		{
			const uintptr_t head = rbx::find_child(character, "Head");
			if (!head) return;
			const uintptr_t prim = prim_of(head);
			if (!prim || !off::Size) return;

			const Vec3 sz = mem::read<Vec3>(prim + off::Size);
			if (sz.x > 0.01f && original_head.x <= 0.01f)
				original_head = sz;
			if (original_head.x > 0.01f)
				mem::write<Vec3>(prim + off::Size, Vec3{ 0.0f, 0.0f, 0.0f });
		}

		void restore_headless(uintptr_t character)
		{
			if (original_head.x <= 0.01f) return;
			const uintptr_t head = rbx::find_child(character, "Head");
			const uintptr_t prim = head ? prim_of(head) : 0;
			if (prim && off::Size)
				mem::write<Vec3>(prim + off::Size, original_head);
			original_head = Vec3{ 0.0f, 0.0f, 0.0f };
		}

		void apply_korblox(uintptr_t character)
		{
			if (original_mesh.empty())
			{
				const uintptr_t rul = rbx::find_child(character, "RightUpperLeg");
				if (rul && o_mesh_id)
				{
					const std::string cur = mem::read_string(rul + o_mesh_id);
					if (!cur.empty() && cur.find('\x01') == std::string::npos) // skip garbage pointers
						original_mesh = cur;
				}
			}

			// transparency on the right foot + calf
			const uintptr_t rf = rbx::find_child(character, "RightFoot");
			if (rf && o_transparency) mem::write<float>(rf + o_transparency, 1.0f);
			const uintptr_t rll = rbx::find_child(character, "RightLowerLeg");
			if (rll && o_transparency) mem::write<float>(rll + o_transparency, 1.0f);

			// korblox mesh on the right upper leg
			const uintptr_t rul = rbx::find_child(character, "RightUpperLeg");
			if (rul && o_mesh_id)
				mem::write_string(rul + o_mesh_id, kKorbloxMesh);

			// R6 rigs have a single "RightLeg" part - no mesh to swap, so fake the
			// look by making the plain leg transparent instead.
			if (!rf && !rll && !rul)
			{
				const uintptr_t rleg = rbx::find_child(character, "RightLeg");
				if (rleg && o_transparency) mem::write<float>(rleg + o_transparency, 1.0f);
			}
		}

		void restore_korblox(uintptr_t character)
		{
			const uintptr_t rf = rbx::find_child(character, "RightFoot");
			if (rf && o_transparency) mem::write<float>(rf + o_transparency, 0.0f);
			const uintptr_t rll = rbx::find_child(character, "RightLowerLeg");
			if (rll && o_transparency) mem::write<float>(rll + o_transparency, 0.0f);
			const uintptr_t rul = rbx::find_child(character, "RightUpperLeg");
			if (rul && o_mesh_id && !original_mesh.empty())
				mem::write_string(rul + o_mesh_id, original_mesh);
			const uintptr_t rleg = rbx::find_child(character, "RightLeg");
			if (rleg && o_transparency) mem::write<float>(rleg + o_transparency, 0.0f);
			original_mesh.clear();
		}

		void apply_recolor(uintptr_t character)
		{
			if (!o_color) return;

			for (const uintptr_t child : rbx::children(character))
			{
				if (!child) continue;
				const std::string cls = rbx::classname(child);
				if (cls.find("Part") == std::string::npos && cls != "MeshPart") continue;
				const std::string nm = rbx::name_of(child);
				if (!recolorable(nm)) continue;

				bool cached = false;
				for (const auto& b : g_colors)
					if (b.addr == child) { cached = true; break; }
				if (!cached)
					g_colors.push_back(ColorBackup{ child, mem::read<Vec3>(child + o_color) });

				const Vec3 tc = target_color(nm);
				mem::write<Vec3>(child + o_color, tc);
			}

			for (const auto& b : g_colors)
			{
				if (!b.addr) continue;
				const Vec3 tc = target_color(rbx::name_of(b.addr));
				mem::write<Vec3>(b.addr + o_color, tc);
			}
		}

		void restore_recolor()
		{
			for (const auto& b : g_colors)
				if (b.addr) mem::write<Vec3>(b.addr + o_color, b.color);
			g_colors.clear();
		}

		void apply_removal(uintptr_t character)
		{
			// restore any items the user just toggled back on
			for (auto it = g_removed.begin(); it != g_removed.end();)
			{
				const bool restore = (it->is_hair && !remove_hair) || (!it->is_hair && !remove_accessories);
				if (restore)
				{
					if (it->addr && o_parent) mem::write<uintptr_t>(it->addr + o_parent, character);
					it = g_removed.erase(it);
				}
				else ++it;
			}

			if (!remove_hair && !remove_accessories) return;
			if (!o_parent) return;

			for (const uintptr_t child : rbx::children(character))
			{
				if (!child || rbx::classname(child) != "Accessory") continue;
				const bool hair = is_hair(child);

				bool cached = false;
				for (const auto& r : g_removed)
					if (r.addr == child) { cached = true; break; }
				if (!cached)
					g_removed.push_back(RemovedItem{ child, hair });

				if ((hair && remove_hair) || (!hair && remove_accessories))
					mem::write<uintptr_t>(child + o_parent, 0);
			}
		}

		void reset_states()
		{
			original_head = Vec3{ 0.0f, 0.0f, 0.0f };
			original_mesh.clear();
			g_colors.clear();
			g_removed.clear();
			was_headless = was_korblox = was_recolor = false;
		}

		void run()
		{
			uintptr_t last_character = 0;
			while (g_running)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(100));

				const uintptr_t character = local_character();
				if (!character)
				{
					last_character = 0;
					reset_states();
					continue;
				}

				if (character != last_character)
				{
					last_character = character;
					reset_states();
				}

				const bool any = headless || korblox || recolor || remove_hair || remove_accessories;
				if (!any)
				{
					if (was_headless) { restore_headless(character); was_headless = false; }
					if (was_korblox) { restore_korblox(character); was_korblox = false; }
					if (was_recolor) { restore_recolor(); was_recolor = false; }
					apply_removal(character); // handles restoring removed items toggled off
					continue;
				}

				if (headless) { apply_headless(character); was_headless = true; }
				else if (was_headless) { restore_headless(character); was_headless = false; }

				if (korblox) { apply_korblox(character); was_korblox = true; }
				else if (was_korblox) { restore_korblox(character); was_korblox = false; }

				if (recolor) { apply_recolor(character); was_recolor = true; }
				else if (was_recolor) { restore_recolor(); was_recolor = false; }

				apply_removal(character);
			}
		}
	} // namespace

	void start()
	{
		if (g_running) return;
		o_transparency = off::find("BasePart.Transparency");
		o_mesh_id = off::find("MeshPart.MeshId");
		o_color = off::find("BasePart.Color");
		if (!o_color) o_color = off::find("BasePart.Color3");
		o_parent = off::find("Instance.Parent");
		if (!o_transparency || !o_mesh_id || !o_color || !off::Size)
			toast::push(toast::Kind::Warn, "Cosmetics: some offsets missing for this build");
		g_running = true;
		g_thread = std::thread(run);
	}

	void shutdown()
	{
		g_running = false;
		if (g_thread.joinable()) g_thread.join();
		reset_states();
	}
} // namespace cosmetic