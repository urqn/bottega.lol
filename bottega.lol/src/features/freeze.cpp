#include "freeze.h"

#include "check.h"
#include "mem.h"
#include "offsets.h"
#include "toast.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

namespace freeze
{
	namespace
	{
		std::thread g_thread;
		std::atomic<bool> g_running{ false };
		std::atomic<bool> g_active{ false };

		uintptr_t o_flag = 0; // TargetTimeDelayFacctorTenths FFlag
		bool warned = false;

		int toggled = 0;
		bool toggle_was_down = false;

		bool key_down(int vk)
		{
			return vk != 0 && (GetAsyncKeyState(vk) & 0x8000) != 0;
		}
	}

	void run()
	{
		bool was = false;
		while (g_running.load(std::memory_order_acquire))
		{
			if (!enabled || o_flag == 0)
			{
				if (o_flag == 0 && enabled && !warned)
				{
					warned = true;
					toast::push(toast::Kind::Warn,
						"freeze player: FFlag offset missing for this build - disabled");
				}
				was = false;
				toggled = 0;
				toggle_was_down = false;
				std::this_thread::sleep_for(std::chrono::milliseconds(200));
				continue;
			}

			// activation gate mirrors the movement gates: 0 hold, 1 toggle, 2 always.
			const int mode = (key_mode < 0 || key_mode > 2) ? 1 : key_mode;
			bool active = false;
			if (mode == 2)
			{
				active = true;
			}
			else if (key == 0)
			{
				active = mode == 0;
			}
			else if (check::blocked())
			{
				active = false;
			}
			else if (mode == 0)
			{
				active = key_down(key);
			}
			else
			{
				const bool down = key_down(key);
				if (down && !toggle_was_down) toggled ^= 1;
				toggle_was_down = down;
				active = toggled != 0;
			}

			if (active != was)
			{
				const uintptr_t base = mem::base;
				if (base && o_flag)
					mem::write<long long>(base + o_flag,
						active ? -999999999999999999LL : 20LL);
				g_active.store(active, std::memory_order_release);
				was = active;
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(50));
		}
	}

	void start()
	{
		if (!enabled || g_running.load(std::memory_order_acquire)) return;

		o_flag = off::find("FFlag.TargetTimeDelayFacctorTenths");
		if (!o_flag)
			o_flag = off::find("TargetTimeDelayFacctorTenths");
		if (!o_flag)
			o_flag = off::find("FFlags.TargetTimeDelayFacctorTenths");

		g_running.store(true, std::memory_order_release);
		g_thread = std::thread(run);
	}

	void shutdown()
	{
		g_running.store(false, std::memory_order_release);
		if (g_thread.joinable()) g_thread.join();

		// leave the avatar unfrozen on exit.
		if (o_flag && mem::base)
		{
			mem::write<long long>(mem::base + o_flag, 20LL);
			g_active.store(false, std::memory_order_release);
		}
	}
} // namespace freeze