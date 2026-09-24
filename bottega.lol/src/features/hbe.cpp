#include "hbe.h"
#include "settings.h"

#include <Windows.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include "mem.h"
#include "offsets.h"
#include "rbx.h"

namespace hbe
{
	namespace
	{
		struct Entry
		{
			std::uintptr_t owner_char;
			Vec3 orig_size;
			bool orig_collide;
		};

		std::unordered_map<std::uintptr_t, Entry> g_saved; // prim -> entry
		std::mutex g_mtx;
		std::thread g_thread;
		std::atomic<bool> g_running{ false };

		std::uintptr_t o_flags = 0;
		std::uintptr_t o_cancollide = 0;

		bool get_collide(std::uintptr_t prim, bool& out)
		{
			if (!prim || !o_flags || !o_cancollide) return false;
			const std::uint8_t flags = mem::read<std::uint8_t>(prim + o_flags);
			out = (flags & static_cast<std::uint8_t>(o_cancollide)) != 0;
			return true;
		}

		void set_collide(std::uintptr_t prim, bool collide)
		{
			if (!prim || !o_flags || !o_cancollide) return;
			std::uint8_t flags = mem::read<std::uint8_t>(prim + o_flags);
			const std::uint8_t bit = static_cast<std::uint8_t>(o_cancollide);
			const std::uint8_t next = collide ? static_cast<std::uint8_t>(flags | bit)
			                                  : static_cast<std::uint8_t>(flags & ~bit);
			if (next != flags)
				mem::write<std::uint8_t>(prim + o_flags, next);
		}

		std::uintptr_t local_character()
		{
			if (!rbx::local_player) return 0;
			return mem::read<std::uintptr_t>(rbx::local_player + off::ModelInstance);
		}

		struct Target
		{
			std::uintptr_t character;
			std::uintptr_t prim;
		};

		std::vector<Target> collect_targets()
		{
			std::vector<Target> out;
			const std::uintptr_t local_char = local_character();
			if (!local_char) return out;

			const std::vector<rbx::Player> players = rbx::players();
			for (const auto& p : players)
			{
				if (!p.character || p.character == local_char) continue;
				if (sv::dead_check && p.health <= 0.0f) continue;
				if (sv::team_check && p.friendly) continue;

				const std::uintptr_t hrp = rbx::find_child(p.character, "HumanoidRootPart");
				if (!hrp || !off::Primitive) continue;
				const std::uintptr_t prim = mem::read<std::uintptr_t>(hrp + off::Primitive);
				if (!prim) continue;

				out.push_back(Target{ p.character, prim });
			}
			return out;
		}

		void apply()
		{
			const std::vector<Target> targets = collect_targets();

			// prune saved entries whose prim is no longer owned by a current target
			{
				std::lock_guard<std::mutex> lock(g_mtx);
				for (auto it = g_saved.begin(); it != g_saved.end();)
				{
					bool found = false;
					for (const auto& t : targets)
					{
						if (t.prim == it->first) { found = true; break; }
					}
					if (found) ++it;
					else it = g_saved.erase(it); // avatar despawned: never touch freed memory
				}
			}

			for (const auto& t : targets)
			{
				std::lock_guard<std::mutex> lock(g_mtx);
				auto& e = g_saved[t.prim];
				e.owner_char = t.character;

				if (e.orig_size.x <= 0.01f) // freshly saved
				{
					e.orig_size = mem::read<Vec3>(t.prim + off::Size);
					if (!get_collide(t.prim, e.orig_collide)) e.orig_collide = true;
				}

				Vec3 n{ e.orig_size.x * scale_x, e.orig_size.y * scale_y, e.orig_size.z * scale_z };
				mem::write<Vec3>(t.prim + off::Size, n);
				if (disable_collision)
					set_collide(t.prim, false);
			}
		}

		void restore()
		{
			const std::vector<Target> targets = collect_targets();
			for (auto& kv : g_saved)
			{
				bool alive = false;
				for (const auto& t : targets)
				{
					if (t.prim == kv.first) { alive = true; break; }
				}
				if (!alive) continue;
				mem::write<Vec3>(kv.first + off::Size, kv.second.orig_size);
				set_collide(kv.first, kv.second.orig_collide);
			}
			g_saved.clear();
		}

		void run()
		{
			while (g_running)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
				if (enabled)
					apply();
				else
					restore();
			}
		}
	} // namespace

	void start()
	{
		if (!enabled || g_running) return;
		o_flags = off::find("Primitive.Flags");
		o_cancollide = off::find("PrimitiveFlags.CanCollide");
		g_running = true;
		g_thread = std::thread(run);
	}

	void shutdown()
	{
		g_running = false;
		if (g_thread.joinable()) g_thread.join();
		restore();
	}
} // namespace hbe