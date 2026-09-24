#include "hit.h"
#include "settings.h"
#include "toast.h"

#include <Windows.h>
#include <mmsystem.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "imgui.h"
#include "mem.h"
#include "offsets.h"
#include "overlay.h"
#include "rbx.h"

#pragma comment(lib, "winmm.lib")

namespace hit
{
	namespace
	{
		std::thread g_thread;
		std::atomic<bool> g_running{ false };

		// ------------------------------------------------------------------
		// synthesized hitsounds (RIFF WAV bytes built at runtime)
		// ------------------------------------------------------------------

		std::vector<uint8_t> make_wav(float freq, float ms, float vol, float attack = 0.01f)
		{
			constexpr int kRate = 44100;
			const int n = static_cast<int>(kRate * ms / 1000.0f);
			std::vector<uint8_t> wav;
			wav.reserve(44 + n * 2);

			auto put32 = [&wav](uint32_t v)
			{
				wav.push_back(static_cast<uint8_t>(v & 0xFF));
				wav.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
				wav.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
				wav.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
			};
			auto put16 = [&wav](uint16_t v)
			{
				wav.push_back(static_cast<uint8_t>(v & 0xFF));
				wav.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
			};

			const uint32_t data_bytes = static_cast<uint32_t>(n * 2);

			wav.push_back('R'); wav.push_back('I'); wav.push_back('F'); wav.push_back('F');
			put32(36 + data_bytes);
			wav.push_back('W'); wav.push_back('A'); wav.push_back('V'); wav.push_back('E');
			wav.push_back('f'); wav.push_back('m'); wav.push_back('t'); wav.push_back(' ');
			put32(16);
			put16(1);            // PCM
			put16(1);            // mono
			put32(kRate);
			put32(kRate * 2);    // byte rate
			put16(2);            // block align
			put16(16);           // bits
			wav.push_back('d'); wav.push_back('a'); wav.push_back('t'); wav.push_back('a');
			put32(data_bytes);

			const float t_step = 1.0f / kRate;
			for (int i = 0; i < n; ++i)
			{
				const float t = static_cast<float>(i) * t_step;
				const float env = std::min(1.0f, t / attack) * std::exp(-3.0f * t / (ms / 1000.0f));
				const float s = std::sin(6.2831853f * freq * t) * env * vol;
				const int16_t sample = static_cast<int16_t>(std::clamp(s, -1.0f, 1.0f) * 32767.0f);
				wav.push_back(static_cast<uint8_t>(sample & 0xFF));
				wav.push_back(static_cast<uint8_t>((sample >> 8) & 0xFF));
			}
			return wav;
		}

		const std::vector<uint8_t>& builtin(bool is_kill, int type)
		{
			struct Key { bool kill; int type; bool operator<(const Key& o) const { return kill != o.kill ? kill < o.kill : type < o.type; } };
			static std::map<Key, std::vector<uint8_t>> cache;
			const Key k{ is_kill, type };
			auto it = cache.find(k);
			if (it != cache.end()) return it->second;

			float freq = 1200.0f, ms = 60.0f;
			if (!is_kill)
			{
				switch (type)
				{
				case 2: freq = 1800.0f; ms = 40.0f; break;
				case 3: freq = 800.0f;  ms = 90.0f; break;
				case 4: freq = 2400.0f; ms = 30.0f; break;
				default: freq = 1200.0f; ms = 60.0f; break;
				}
			}
			else
			{
				switch (type)
				{
				case 2: freq = 300.0f;  ms = 250.0f; break;
				case 3: freq = 1500.0f; ms = 90.0f;  break;
				case 4: freq = 100.0f;  ms = 300.0f; break;
				default: freq = 500.0f; ms = 160.0f; break;
				}
			}

			auto inserted = cache.emplace(k, make_wav(freq, ms, 0.6f));
			return inserted.first->second;
		}

		void play(const std::vector<uint8_t>& wav, bool SND_MEMORY_)
		{
			PlaySoundA(reinterpret_cast<LPCSTR>(wav.data()), NULL, SND_ASYNC | (SND_MEMORY_ ? SND_MEMORY : SND_FILENAME));
		}

		void play_sound(int type, float volume, const std::string& custom_path, bool is_kill)
		{
			const WORD chan = static_cast<WORD>(std::clamp(volume, 0.0f, 1.0f) * 0xFFFF);
			waveOutSetVolume(NULL, (static_cast<DWORD>(chan) << 16) | chan);

			if (type == 0 && !custom_path.empty())
			{
				PlaySoundA(custom_path.c_str(), NULL, SND_ASYNC | SND_FILENAME);
				return;
			}
			play(builtin(is_kill, std::max(1, type)), true);
		}

		// ------------------------------------------------------------------
		// hit markers
		// ------------------------------------------------------------------

		struct Marker
		{
			std::chrono::steady_clock::time_point born;
			Vec2 hit_scr;
			float damage;
			bool kill;
			std::string name;
		};

		std::deque<Marker> g_markers;
		std::mutex g_markers_mtx;

		void push_marker(const Vec2& scr, float damage, bool kill, const std::string& name)
		{
			std::lock_guard<std::mutex> lock(g_markers_mtx);
			if (g_markers.size() > 16) g_markers.pop_front();
			g_markers.push_back(Marker{ std::chrono::steady_clock::now(), scr, damage, kill, name });
		}

		// ------------------------------------------------------------------
		// detection
		// ------------------------------------------------------------------

		uintptr_t local_character()
		{
			if (!rbx::local_player) return 0;
			return mem::read<uintptr_t>(rbx::local_player + off::ModelInstance);
		}

		Vec3 part_pos(uintptr_t part)
		{
			Vec3 out{};
			if (!part || !off::Primitive) return out;
			const uintptr_t prim = mem::read<uintptr_t>(part + off::Primitive);
			if (!prim) return out;
			return mem::read<Vec3>(prim + off::Position);
		}

		void run()
		{
			std::map<uintptr_t, float> prev_health;          // player addr -> last seen health
			std::map<uintptr_t, std::chrono::steady_clock::time_point> targeted;
			std::map<uintptr_t, bool> hit_once;              // we actually dealt damage to them
			std::map<uintptr_t, rbx::Player> last_seen;      // info of players that vanished mid-tick

			while (g_running)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(30));
				if (!enabled)
				{
					prev_health.clear();
					targeted.clear();
					last_seen.clear();
					continue;
				}

				const std::vector<rbx::Player> players = rbx::players();
				const uintptr_t local_char = local_character();
				const auto now = std::chrono::steady_clock::now();

				// who is the player firing at right now? (pointer fallback)
				const bool firing = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
				uintptr_t pointed = 0;
				if (firing)
				{
					const Mat4 vm = rbx::view_matrix();
					const Vec2 dims = rbx::viewport();

					Vec2 mx{ dims.x * 0.5f, dims.y * 0.5f };
					HWND hwnd = overlay::target;
					POINT pt{};
					if (hwnd && GetCursorPos(&pt) && ScreenToClient(hwnd, &pt))
					{
						RECT rc{};
						if (GetClientRect(hwnd, &rc) && pt.x >= 0 && pt.y >= 0 && pt.x <= rc.right && pt.y <= rc.bottom)
							mx = Vec2{ static_cast<float>(pt.x), static_cast<float>(pt.y) };
					}

					// generous aimed radius: hitscan games don't point at the exact
					// hitbox, and Arsenal recoil/spread keep the crosshair a few px wide
					float best = 170.0f * 170.0f;
					for (const auto& p : players)
					{
						if (!p.character || p.character == local_char) continue;
						if (sv::dead_check && p.health <= 0.0f) continue;
						if (sv::team_check && p.friendly) continue;

						Vec3 w = p.pos;
						const uintptr_t head = rbx::find_child(p.character, "Head");
						if (head && off::Primitive)
						{
							const Vec3 hp = part_pos(head);
							if (hp.x != 0.0f || hp.y != 0.0f || hp.z != 0.0f) w = hp;
						}

						Vec2 scr{};
						if (!rbx::w2s(w, scr, vm, dims.x, dims.y)) continue;
						const float dx = scr.x - mx.x, dy = scr.y - mx.y;
						const float d = dx * dx + dy * dy;
						if (d < best) { best = d; pointed = p.addr; }
					}
				}

				if (pointed)
					targeted[pointed] = now;

				// prune expired targeting marks (keep them around long enough to
				// catch the death tick of an autokill / DOT)
				constexpr auto kTargetWindow = std::chrono::milliseconds(700);
				for (auto it = targeted.begin(); it != targeted.end();)
				{
					if (now - it->second > kTargetWindow) it = targeted.erase(it);
					else ++it;
				}

				struct Pending { uintptr_t addr; float damage; bool kill; Vec3 hit_pos; std::string name; };
				std::vector<Pending> pending;

				// player address set for vanish detection
				std::vector<uintptr_t> addrs;
				addrs.reserve(players.size());
				for (const auto& p : players)
					if (p.addr) addrs.push_back(p.addr);
				std::sort(addrs.begin(), addrs.end());

				// players whose character already despawned: the death frame most
				// games land on. they read character==0 with a fresh 0 health, but
				// if we had a previous alive baseline and were aiming at them, that
				// is a kill (single-tick despawn before our 30ms next sample).
				for (const auto& p : players)
				{
					if (!p.addr || p.addr == rbx::local_player) continue;
					if (p.character && p.health > 0.1f) continue; // clearly alive
					auto it = prev_health.find(p.addr);
					if (it == prev_health.end() || it->second <= 0.1f) continue;
					const auto tag = targeted.find(p.addr);
					const bool tagged_recently = tag != targeted.end() && (now - tag->second) <= kTargetWindow;
					if (tagged_recently)
					{
						const std::string nm = p.display.empty() ? p.name : p.display;
						pending.push_back({ p.addr, it->second, true, p.pos, nm });
						prev_health.erase(p.addr);
						last_seen.erase(p.addr);
						hit_once.erase(p.addr);
						targeted.erase(p.addr);
					}
				}

				for (const auto& p : players)
				{
					if (!p.character || p.character == local_char || !p.addr) continue;
					// dead rows (health == 0) are deliberately processed: a kill that
					// lands in a single tick reads exactly 100 -> 0, and skipping the
					// 0 row used to swallow every one-shot kill before the despawn.

					auto it = prev_health.find(p.addr);
					if (it == prev_health.end())
					{
						prev_health[p.addr] = p.health;
						continue;
					}

					const float before = it->second;
					if (before - p.health > 0.1f)
					{
						const bool mine = targeted.find(p.addr) != targeted.end();
						if (mine)
						{
							const float dmg = before - p.health;
							const bool killed = p.health <= 0.1f;
							targeted.erase(p.addr);
							hit_once[p.addr] = true;
							const std::string nm = p.display.empty() ? p.name : p.display;
							pending.push_back({ p.addr, dmg, killed, p.pos, nm });
							if (killed)
							{
								// kill consumed: drop tracking so the vanish pass
								// below cannot double-report the same death
								prev_health.erase(p.addr);
								last_seen.erase(p.addr);
								continue;
							}
						}
					}
					else if (p.health - before > 0.1f)
					{
						// respawned / healed - fresh baseline
					}

					it->second = p.health;
					last_seen[p.addr] = p;
				}

				// players that vanished from the snapshot mid-tick: the cleanest
				// explanation is a kill whose character despawned before our next
				// sample. only call it if we had them tagged, already tagged them
				// once this fight document, or they were under 25hp.
				for (auto it = prev_health.begin(); it != prev_health.end();)
				{
					const bool still_here = std::binary_search(addrs.begin(), addrs.end(), it->first);
					if (still_here) { ++it; continue; }

					const auto tag = targeted.find(it->first);
					const bool tagged_recently = tag != targeted.end() && (now - tag->second) <= kTargetWindow;
					const bool low = it->second <= 25.0f;
					const bool wounded = hit_once.find(it->first) != hit_once.end();
					const auto seen = last_seen.find(it->first);
					if (tagged_recently && (low || wounded) && seen != last_seen.end())
					{
						const std::string nm = seen->second.display.empty() ? seen->second.name : seen->second.display;
						pending.push_back({ it->first, it->second, true, seen->second.pos, nm });
					}
					targeted.erase(it->first);
					last_seen.erase(it->first);
					hit_once.erase(it->first);
					it = prev_health.erase(it);
				}

				for (const auto& ev : pending)
				{
					if (ev.kill)
					{
						if (killsound_enabled)
							play_sound(killsound_type, killsound_volume, killsound_path, true);
						if (hit_toasts)
							toast::push_kill(ev.name, {});
					}
					else
					{
						if (sounds_enabled)
							play_sound(hitsound_type, hitsound_volume, hitsound_path, false);
						if (hit_toasts)
							toast::push_hit(ev.name, ev.damage);
					}

					if (markers_enabled)
					{
						const Vec2 dims = rbx::viewport();
						const Mat4 vm = rbx::view_matrix();
						const Vec3 head = ev.hit_pos;
						Vec2 scr{};
						if (rbx::w2s(head, scr, vm, dims.x, dims.y))
							push_marker(scr, ev.damage, ev.kill, ev.name);
					}
				}
			}
		}
	} // namespace

	void render(ImDrawList* dl, const Mat4& vm, const Vec2& dims)
	{
		if (!markers_enabled || !dl) return;

		const auto now = std::chrono::steady_clock::now();
		const Vec2 center{ dims.x * 0.5f, dims.y * 0.5f };

		std::lock_guard<std::mutex> lock(g_markers_mtx);
		for (auto it = g_markers.begin(); it != g_markers.end();)
		{
			const float elapsed = std::chrono::duration<float>(now - it->born).count();
			if (elapsed >= marker_lifetime)
			{
				it = g_markers.erase(it);
				continue;
			}

			const float p = 1.0f - elapsed / marker_lifetime;
			const int a = static_cast<int>(255.0f * std::clamp(p, 0.0f, 1.0f));

			dl->AddLine(ImVec2{ center.x, center.y }, ImVec2{ it->hit_scr.x, it->hit_scr.y },
				IM_COL32(255, 90, 90, a), 2.0f);

			if (damage_text && it->damage > 0.0f)
			{
				const float rise = (1.0f - p) * 28.0f;
				char buf[96];
				std::snprintf(buf, sizeof(buf), "-%d", static_cast<int>(std::lround(it->damage)));
				std::string txt = buf;
				if (it->kill) txt += " [KILL]";

				const ImVec2 sz = ImGui::CalcTextSize(txt.c_str());
				const ImVec2 pos{ it->hit_scr.x - sz.x * 0.5f, it->hit_scr.y - 24.0f - rise };
				dl->AddText(ImVec2{ pos.x + 1, pos.y + 1 }, IM_COL32(0, 0, 0, a), txt.c_str());
				dl->AddText(pos, IM_COL32(255, 255, 255, a), txt.c_str());
			}

			++it;
		}
	}

	void start()
	{
		if (!enabled || g_running) return;
		g_running = true;
		g_thread = std::thread(run);
	}

	void shutdown()
	{
		g_running = false;
		if (g_thread.joinable()) g_thread.join();
	}
} // namespace hit