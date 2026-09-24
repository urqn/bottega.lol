#include "trigger.h"
#include "check.h"
#include "settings.h"

#include <Windows.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "mem.h"
#include "offsets.h"
#include "overlay.h"
#include "rbx.h"

namespace trigger
{
	namespace
	{
		struct Mat3
		{
			float m[9];
		};

		struct Ray3D
		{
			Vec3 origin;
			Vec3 dir;
		};

		std::thread g_thread;
		std::atomic<bool> g_running{ false };
		std::atomic<bool> g_fire{ false };

		std::vector<rbx::Player> g_players;
		std::mutex g_players_mtx;
		std::chrono::steady_clock::time_point g_last_refresh;

		static int toggled = 0;
		static bool toggle_was_down = false;

		bool key_down(int vk)
		{
			return vk != 0 && (GetAsyncKeyState(vk) & 0x8000) != 0;
		}

		bool roblox_focused()
		{
			const HWND fg = GetForegroundWindow();
			return fg && (fg == overlay::target || fg == overlay::hwnd);
		}

		bool ray_intersects_obb(const Ray3D& ray, const Vec3& center, const Vec3& size,
			const Mat3& rot, float max_dist, float& out_t)
		{
			const float half[3] = { size.x * 0.5f, size.y * 0.5f, size.z * 0.5f };
			const Vec3 p{ center.x - ray.origin.x, center.y - ray.origin.y, center.z - ray.origin.z };

			const float axes[3][3] = {
				{ rot.m[0], rot.m[1], rot.m[2] },
				{ rot.m[3], rot.m[4], rot.m[5] },
				{ rot.m[6], rot.m[7], rot.m[8] },
			};

			float tmin = 0.0f;
			float tmax = max_dist;

			for (int i = 0; i < 3; ++i)
			{
				const float e = axes[i][0] * p.x + axes[i][1] * p.y + axes[i][2] * p.z;
				const float f = axes[i][0] * ray.dir.x + axes[i][1] * ray.dir.y + axes[i][2] * ray.dir.z;

				const float EPS = 1e-6f;
				if (std::fabs(f) > EPS)
				{
					float t1 = (e - half[i]) / f;
					float t2 = (e + half[i]) / f;
					if (t1 > t2) { const float tmp = t1; t1 = t2; t2 = tmp; }

					if (tmin < t1) tmin = t1;
					if (tmax > t2) tmax = t2;
					if (tmax < tmin) return false;
				}
				else
				{
					if ((-e - half[i] > 0.0f) || (-e + half[i] < 0.0f)) return false;
				}
			}

			if (tmax < 0.0f) return false;
			out_t = (tmin >= 0.0f) ? tmin : tmax;
			return true;
		}

		uintptr_t o_cam_fov = 0;

		bool prim_data(uintptr_t part, Vec3& pos, Vec3& size, Mat3& rot)
		{
			if (!part || !off::Primitive) return false;
			const uintptr_t prim = mem::read<uintptr_t>(part + off::Primitive);
			if (!prim) return false;
			pos = mem::read<Vec3>(prim + off::Position);
			size = mem::read<Vec3>(prim + off::Size);
			rot = mem::read<Mat3>(prim + off::Rotation);
			return size.x > 0.01f && size.y > 0.01f && size.z > 0.01f;
		}

		uintptr_t find_part(uintptr_t character, const char* name)
		{
			if (!character) return 0;
			return rbx::find_child(character, name);
		}

		uintptr_t local_character()
		{
			if (!rbx::local_player) return 0;
			return mem::read<uintptr_t>(rbx::local_player + off::ModelInstance);
		}

		bool is_gun_equipped()
		{
			const uintptr_t ch = local_character();
			if (!ch) return false;
			if (rbx::find_child_byclass(ch, "Tool")) return true;

			const uintptr_t cam = rbx::fresh_camera();
			if (!cam) return false;
			for (const uintptr_t c : rbx::children(cam))
			{
				if (!c) continue;
				const std::string cls = rbx::classname(c);
				if (cls == "Tool" || cls == "Model")
				{
					std::string nm = rbx::name_of(c);
					for (auto& ch2 : nm) ch2 = static_cast<char>(tolower(ch2));
					if (nm.find("viewmodel") != std::string::npos || nm.find("gun") != std::string::npos ||
						nm.find("weapon") != std::string::npos || nm.find("arms") != std::string::npos)
						return true;
				}
			}
			return false;
		}

		std::vector<const char*> part_names(int tpart)
		{
			std::vector<const char*> names;
			switch (tpart)
			{
			case 1: // Torso
				names.push_back("UpperTorso"); names.push_back("Torso"); names.push_back("LowerTorso");
				break;
			case 2: // Root
				names.push_back("HumanoidRootPart");
				break;
			case 3: // Left Arm
				names.push_back("LeftUpperArm"); names.push_back("Left Arm"); names.push_back("LeftLowerArm"); names.push_back("LeftHand");
				break;
			case 4: // Right Arm
				names.push_back("RightUpperArm"); names.push_back("Right Arm"); names.push_back("RightLowerArm"); names.push_back("RightHand");
				break;
			case 5: // Left Leg
				names.push_back("LeftUpperLeg"); names.push_back("Left Leg"); names.push_back("LeftLowerLeg"); names.push_back("LeftFoot");
				break;
			case 6: // Right Leg
				names.push_back("RightUpperLeg"); names.push_back("Right Leg"); names.push_back("RightLowerLeg"); names.push_back("RightFoot");
				break;
			case 7: // All limbs
				names.push_back("LeftUpperArm"); names.push_back("Left Arm"); names.push_back("RightUpperArm"); names.push_back("Right Arm");
				names.push_back("LeftUpperLeg"); names.push_back("Left Leg"); names.push_back("RightUpperLeg"); names.push_back("Right Leg");
				break;
			default: // Head
				names.push_back("Head");
				break;
			}
			return names;
		}

		bool aimable(const rbx::Player& p, uintptr_t local_char)
		{
			if (!p.character || p.character == local_char) return false;
			if (sv::dead_check && p.health <= 0.0f) return false;
			if (sv::team_check && p.friendly) return false;
			return true;
		}

		void refresh_players()
		{
			std::lock_guard<std::mutex> lock(g_players_mtx);
			g_players = rbx::players();
			g_last_refresh = std::chrono::steady_clock::now();
		}

		void raycast_loop(uintptr_t local_char)
		{
			const uintptr_t cam = rbx::fresh_camera();
			if (!cam) return;

			const Vec3 cam_pos = mem::read<Vec3>(cam + off::CameraPos);
			if (cam_pos.x == 0.0f && cam_pos.y == 0.0f && cam_pos.z == 0.0f) return;

			const Mat3 cr = mem::read<Mat3>(cam + off::CameraRotation);

			// roblox camera: forward = -row2, right = row0, up = row1 (as ivory)
			Vec3 look{ -cr.m[6], -cr.m[7], -cr.m[8] };
			Vec3 right{ cr.m[0], cr.m[1], cr.m[2] };
			Vec3 up{ cr.m[3], cr.m[4], cr.m[5] };

			const float ll = std::sqrt(look.x * look.x + look.y * look.y + look.z * look.z);
			const float rl = std::sqrt(right.x * right.x + right.y * right.y + right.z * right.z);
			const float ul = std::sqrt(up.x * up.x + up.y * up.y + up.z * up.z);
			if (ll > 1e-5f) { look.x /= ll; look.y /= ll; look.z /= ll; }
			if (rl > 1e-5f) { right.x /= rl; right.y /= rl; right.z /= rl; }
			if (ul > 1e-5f) { up.x /= ul; up.y /= ul; up.z /= ul; }

			float fov = 70.0f;
			if (o_cam_fov)
			{
				const float f = mem::read<float>(cam + o_cam_fov);
				if (f > 1.0f && f <= 170.0f) fov = f;
			}

			const Vec2 dims = rbx::viewport();
			if (dims.x < 100.0f || dims.y < 100.0f) return;

			Vec2 crosshair{ dims.x * 0.5f, dims.y * 0.5f };
			{
				HWND hwnd = overlay::target;
				POINT pt{};
				if (hwnd && GetCursorPos(&pt) && ScreenToClient(hwnd, &pt))
				{
					RECT rc{};
					if (GetClientRect(hwnd, &rc) && pt.x >= 0 && pt.y >= 0 && pt.x <= rc.right && pt.y <= rc.bottom)
						crosshair = Vec2{ static_cast<float>(pt.x), static_cast<float>(pt.y) };
				}
			}

			const float tan_fov = std::tanf(static_cast<float>(3.14159265358979323846 * fov / 180.0) * 0.5f);
			const float ndc_x = (crosshair.x - dims.x * 0.5f) / (dims.y * 0.5f);
			const float ndc_y = (dims.y * 0.5f - crosshair.y) / (dims.y * 0.5f);

			Vec3 rd{
				look.x + right.x * (ndc_x * tan_fov) + up.x * (ndc_y * tan_fov),
				look.y + right.y * (ndc_x * tan_fov) + up.y * (ndc_y * tan_fov),
				look.z + right.z * (ndc_x * tan_fov) + up.z * (ndc_y * tan_fov),
			};
			const float dl = std::sqrt(rd.x * rd.x + rd.y * rd.y + rd.z * rd.z);
			if (dl > 1e-5f) { rd.x /= dl; rd.y /= dl; rd.z /= dl; }

			const Ray3D ray{ cam_pos, rd };
			const float padding = (std::max)(0.0f, (threshold - 1.0f) * 0.05f);
			const float max_dist = 1000.0f;
			float closest_t = max_dist;

			const std::vector<const char*> names = part_names(target_part);
			if (names.empty()) return;

			std::vector<rbx::Player> players;
			{
				std::lock_guard<std::mutex> lock(g_players_mtx);
				players = g_players;
			}

			for (const auto& p : players)
			{
				if (!aimable(p, local_char)) continue;

				for (const char* nm : names)
				{
					const uintptr_t part = find_part(p.character, nm);
					if (!part) continue;

					Vec3 c{}, sz{};
					Mat3 r{};
					if (!prim_data(part, c, sz, r)) continue;

					const Vec3 padded{ sz.x + padding, sz.y + padding, sz.z + padding };
					float t = 0.0f;
					if (ray_intersects_obb(ray, c, padded, r, closest_t, t))
					{
						closest_t = t;
						g_fire.store(true, std::memory_order_release);
						return;
					}
				}
			}
		}

		void screen_loop(uintptr_t local_char)
		{
			const Vec2 dims = rbx::viewport();
			if (dims.x < 100.0f || dims.y < 100.0f) return;

			const Mat4 vm = rbx::view_matrix();
			const float thresh = (std::max)(1.0f, threshold);
			const float thresh_sq = thresh * thresh;

			const std::vector<const char*> names = part_names(target_part);
			if (names.empty()) return;

			std::vector<rbx::Player> players;
			{
				std::lock_guard<std::mutex> lock(g_players_mtx);
				players = g_players;
			}

			for (const auto& p : players)
			{
				if (!aimable(p, local_char)) continue;

				for (const char* nm : names)
				{
					const uintptr_t part = find_part(p.character, nm);
					if (!part) continue;

					Vec3 world{}, sz{};
					Mat3 r{};
					if (!prim_data(part, world, sz, r)) continue;

					Vec2 scr{};
					if (!rbx::w2s(world, scr, vm, dims.x, dims.y)) continue;

					const float dx = scr.x - dims.x * 0.5f;
					const float dy = scr.y - dims.y * 0.5f;
					if (dx * dx + dy * dy <= thresh_sq)
					{
						g_fire.store(true, std::memory_order_release);
						return;
					}
				}
			}
		}

		void fire_click()
		{
			INPUT in{};
			in.type = INPUT_MOUSE;
			in.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
			SendInput(1, &in, sizeof(INPUT));
			in.mi.dwFlags = MOUSEEVENTF_LEFTUP;
			SendInput(1, &in, sizeof(INPUT));
		}

		void run()
		{
			refresh_players();
			while (g_running)
			{
				if (!enabled)
				{
					toggled = 0;
					toggle_was_down = false;
					std::this_thread::sleep_for(std::chrono::milliseconds(50));
					continue;
				}

				// activation gate mirrors the movement gates: 0 hold, 1 toggle, 2 always.
				const int mode = (key_mode < 0 || key_mode > 2) ? 1 : key_mode;
				bool active = false;
				if (mode == 2)
				{
					active = true; // always on, key ignored
				}
				else if (key == 0)
				{
					active = mode == 0; // hold with no key = always-on, toggle with no key = off
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
				if (!active)
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(5));
					continue;
				}

				if (!roblox_focused())
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(10));
					continue;
				}

				const auto now = std::chrono::steady_clock::now();
				if (now - g_last_refresh >= std::chrono::milliseconds(15))
					refresh_players();

				const uintptr_t local_char = local_character();
				if (!local_char)
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(10));
					continue;
				}

				if (gun_check && !is_gun_equipped())
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(10));
					continue;
				}

				g_fire.store(false, std::memory_order_release);
				if (method == 1)
					raycast_loop(local_char);
				else
					screen_loop(local_char);

				if (g_fire.load(std::memory_order_acquire))
				{
					if (delay_ms > 0.0f)
						std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(delay_ms)));
					fire_click();
					const int cd = (std::max)(5, static_cast<int>(cooldown_ms));
					std::this_thread::sleep_for(std::chrono::milliseconds(cd));
				}
				else
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
				}
			}
		}
	} // namespace

	std::vector<std::string> part_labels()
	{
		return {
			"Head",
			"Torso",
			"Root",
			"Left Arm",
			"Right Arm",
			"Left Leg",
			"Right Leg",
			"Arms + Legs",
		};
	}

	void start()
	{
		if (!enabled || g_running) return;
		o_cam_fov = off::find("Camera.FieldOfView");
		g_running = true;
		g_thread = std::thread(run);
	}

	void shutdown()
	{
		g_running = false;
		if (g_thread.joinable()) g_thread.join();
	}
} // namespace trigger