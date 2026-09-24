#include "check.h"

#include "mem.h"
#include "offsets.h"
#include "rbx.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <thread>

namespace check
{
	namespace
	{
		std::thread g_thread;
		std::atomic<bool> g_running{ false };

		std::uintptr_t o_typing = 0; // ChatInputBarConfiguration open byte (0x156 in ivory's build)
	}

	bool blocked()
	{
		return enabled && textchatopen.load(std::memory_order_acquire);
	}

	void run()
	{
		int last_update = 0;
		std::uint64_t last_datamodel = 0;
		uintptr_t cibc = 0;

		while (g_running.load(std::memory_order_acquire))
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(10));

			const uintptr_t dm = rbx::datamodel;
			if (!dm || dm == 0)
			{
				cibc = 0;
				textchatopen.store(false, std::memory_order_release);
				continue;
			}

			if (dm != last_datamodel)
			{
				last_datamodel = dm;
				cibc = 0;
			}

			// re-resolve the services every ~100ms; they only change when the
			// datamodel tears down (place join), which we catch above.
			if (last_update == 0 || (last_update % 10) == 0)
			{
				const uintptr_t tcs = rbx::find_child_byclass(dm, "TextChatService");
				cibc = 0;
				if (tcs)
					cibc = rbx::find_child(tcs, "ChatInputBarConfiguration");
			}

			bool open = false;
			if (cibc && o_typing)
			{
				const uint8_t val = mem::read<uint8_t>(cibc + o_typing);
				open = (val == 1);
			}
			textchatopen.store(open, std::memory_order_release);

			last_update++;
		}
	}

	void start()
	{
		if (!enabled || g_running.load(std::memory_order_acquire)) return;
		o_typing = off::find("ChatInputBarConfiguration.IsOpen");
		if (!o_typing)
			o_typing = 0x156; // ivory's layout (read-only, harmless if version differs)
		g_running.store(true, std::memory_order_release);
		g_thread = std::thread(run);
	}

	void shutdown()
	{
		g_running.store(false, std::memory_order_release);
		if (g_thread.joinable()) g_thread.join();
		textchatopen.store(false, std::memory_order_release);
	}
} // namespace check