#include "aim.h"
#include "check.h"
#include "settings.h"

#include <Windows.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <cstring>
#include <cwchar>
#include <mutex>
#include <string>

#include "imgui.h"
#include "mem.h"
#include "offsets.h"
#include "overlay.h"
#include "silent_ray.h"

namespace aim
{
	namespace
	{
		struct Vec2i16
		{
			std::int16_t x = 0;
			std::int16_t y = 0;
		};

		struct Mat3
		{
			float m[9];
		};

		std::mutex g_mutex;

		std::atomic<bool> g_write_active{ false };
		std::atomic<bool> g_write_stop{ false };
		std::atomic<bool> g_write_spoofed{ false };
		std::atomic<float> g_write_x{ 0.0f };
		std::atomic<float> g_write_y{ 0.0f };
		std::atomic<float> g_write_z{ 0.0f };

		HANDLE g_thread{ nullptr };
		Vec2i16 g_last{};
		int g_fails{ 0 };

		std::atomic<bool> g_rot_active{ false };
		std::mutex g_rot_mutex;
		Mat3 g_rot{};

		float g_dist{ 0.0f };
		float g_speed{ 0.0f };

		Vec2 s_tracer_pos{};
		bool s_tracer_valid{ false };

		bool roblox_focused()
		{
			const HWND fg = GetForegroundWindow();
			return fg && (fg == overlay::target || fg == overlay::hwnd);
		}

		bool key_down(int vk)
		{
			return vk != 0 && (GetAsyncKeyState(vk) & 0x8000) != 0;
		}

		Vec3 part_pos(uintptr_t part)
		{
			Vec3 out{};
			if (!part || !off::Primitive) return out;
			const uintptr_t prim = mem::read<uintptr_t>(part + off::Primitive);
			if (!prim) return out;
			return mem::read<Vec3>(prim + off::Position);
		}

		std::string part_name(uintptr_t part)
		{
			if (!part) return "";
			return rbx::name_of(part);
		}

		uintptr_t find_part(uintptr_t character, const char* name)
		{
			if (!character) return 0;
			return rbx::find_child(character, name);
		}

		bool screen_of(const Vec3& world, const Mat4& vm, const Vec2& size, Vec2& out)
		{
			return rbx::w2s(world, out, vm, size.x, size.y);
		}

		struct BodyPartDef
		{
			const char* label;
			const char* const* names;
			std::size_t count;
		};

		const char* const k_head_name[]  = { "Head" };
		const char* const k_root_name[]  = { "HumanoidRootPart" };
		const char* const k_torso_name[] = { "UpperTorso", "Torso", "LowerTorso" };
		const char* const k_larm_name[]  = { "LeftUpperArm", "Left Arm", "LeftLowerArm", "LeftHand" };
		const char* const k_rarm_name[]  = { "RightUpperArm", "Right Arm", "RightLowerArm", "RightHand" };
		const char* const k_lleg_name[]  = { "LeftUpperLeg", "Left Leg", "LeftLowerLeg", "LeftFoot" };
		const char* const k_rleg_name[]  = { "RightUpperLeg", "Right Leg", "RightLowerLeg", "RightFoot" };

		// dropdown value == index. "Nearest" (3) is special-cased in aim_point.
		const BodyPartDef k_body_parts[] = {
			{ "Head",      k_head_name,  std::size(k_head_name) },
			{ "Root",      k_root_name,  std::size(k_root_name) },
			{ "Torso",     k_torso_name, std::size(k_torso_name) },
			{ "Nearest",   nullptr,      0 },
			{ "Left Arm",  k_larm_name,  std::size(k_larm_name) },
			{ "Right Arm", k_rarm_name,  std::size(k_rarm_name) },
			{ "Left Leg",  k_lleg_name,  std::size(k_lleg_name) },
			{ "Right Leg", k_rleg_name,  std::size(k_rleg_name) },
		};

		Vec3 aim_point(rbx::Player& p, int part, const Mat4& vm, const Vec2& size, float& out_dist)
		{
			out_dist = 0.0f;
			if (!p.character) return {};

			const uintptr_t head = find_part(p.character, "Head");
			const Vec3 head_pos = head ? part_pos(head) : p.pos;

			if (part == 3)
			{
				const uintptr_t root = p.hrp;
				const Vec3 root_pos = root ? part_pos(root) : p.pos;

				Vec2 hs{}, rs{};
				const bool has_head = screen_of(head_pos, vm, size, hs);
				const bool has_root = screen_of(root_pos, vm, size, rs);
				if (has_head && has_root)
				{
					const float cx = size.x * 0.5f;
					const float cy = size.y * 0.5f;
					const float dh = (hs.x - cx) * (hs.x - cx) + (hs.y - cy) * (hs.y - cy);
					const float dr = (rs.x - cx) * (rs.x - cx) + (rs.y - cy) * (rs.y - cy);
					return dh <= dr ? head_pos : root_pos;
				}
				return has_head ? head_pos : root_pos;
			}

			if (part >= 0 && part < static_cast<int>(std::size(k_body_parts)))
			{
				const BodyPartDef& def = k_body_parts[part];
				for (std::size_t i = 0; i < def.count; ++i)
				{
					const uintptr_t found = find_part(p.character, def.names[i]);
					if (found) return part_pos(found);
				}
			}

			return head_pos;
		}

		Vec3 predict_point(rbx::Player& p, Vec3 point, float speed)
		{
			if (!prediction || speed <= 1.0f || !p.hrp || !off::Primitive || !off::Velocity) return point;

			const uintptr_t prim = mem::read<uintptr_t>(p.hrp + off::Primitive);
			if (!prim) return point;

			const Vec3 vel = mem::read<Vec3>(prim + off::Velocity);
			const float t = g_dist / speed;
			if (t < 0.0f || t > 2.0f) return point;

			return Vec3{ point.x + vel.x * t, point.y + vel.y * t, point.z + vel.z * t };
		}

		void move_mouse(int dx, int dy)
		{
			if (dx == 0 && dy == 0) return;
			INPUT in{};
			in.type = INPUT_MOUSE;
			in.mi.dx = dx;
			in.mi.dy = dy;
			in.mi.dwFlags = MOUSEEVENTF_MOVE;
			SendInput(1, &in, sizeof(INPUT));
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

		void write_rotation(uintptr_t cam)
		{
			if (!cam || !off::CameraRotation) return;

			Mat3 local{};
			{
				std::lock_guard<std::mutex> lock(g_rot_mutex);
				local = g_rot;
			}

			WriteProcessMemory(mem::hProc, reinterpret_cast<LPVOID>(cam + off::CameraRotation), &local, sizeof(Mat3), NULL);
		}

		void write_viewport(uintptr_t cam, const Vec2i16& v)
		{
			if (!cam || !off::CameraViewport) return;
			mem::write<Vec2i16>(cam + off::CameraViewport, v);
			g_write_spoofed.store(true, std::memory_order_release);
		}

		// restore the spoofed viewport from the *live* visual engine size. the
		// camera may already be gone when a match ends, so fail quietly.
		void restore_viewport(uintptr_t cam)
		{
			if (!cam || !off::CameraViewport) return;
			const Vec2 size = rbx::viewport();
			if (size.x < 1.0f || size.y < 1.0f) return;

			const Vec2i16 v{ static_cast<std::int16_t>(std::lround(size.x)), static_cast<std::int16_t>(std::lround(size.y)) };
			mem::write<Vec2i16>(cam + off::CameraViewport, v);
		}

		Vec2i16 calc_viewport(float tx, float ty, float dw, float dh, float mx, float my)
		{
			double TargetY = static_cast<double>(ty);
			if (TargetY < 1.0) TargetY = 1.0;
			if (TargetY > static_cast<double>(dh) - 1.0) TargetY = static_cast<double>(dh) - 1.0;

			double Ratio = static_cast<double>(my) / TargetY;
			double VY = static_cast<double>(dh) * Ratio;

			if (VY > 32767.0) VY = 32767.0;
			if (VY < 1.0) VY = 1.0;

			Ratio = VY / static_cast<double>(dh);
			double VX = 2.0 * static_cast<double>(mx) - Ratio * (2.0 * static_cast<double>(tx) - static_cast<double>(dw));

			if (VX > 32767.0) VX = 32767.0;
			if (VX < 1.0) VX = 1.0;

			return { static_cast<std::int16_t>(std::round(VX)), static_cast<std::int16_t>(std::round(VY)) };
		}

		bool mouse_in_viewport(HWND hwnd, float dw, float dh, float& mx, float& my)
		{
			if (!hwnd || !IsWindow(hwnd)) return false;

			POINT pt{};
			if (!GetCursorPos(&pt) || !ScreenToClient(hwnd, &pt)) return false;

			RECT cr{};
			if (!GetClientRect(hwnd, &cr)) return false;

			const float cw = static_cast<float>(cr.right - cr.left);
			const float ch = static_cast<float>(cr.bottom - cr.top);
			if (cw < 1.0f || ch < 1.0f) return false;
			if (pt.x < 0 || pt.y < 0 || pt.x >= cr.right || pt.y >= cr.bottom) return false;

			mx = static_cast<float>(pt.x) * (dw / cw);
			my = static_cast<float>(pt.y) * (dh / ch);
			if (mx > dw - 1.0f) mx = dw - 1.0f;
			if (mx < 1.0f) mx = 1.0f;
			if (my > dh - 1.0f) my = dh - 1.0f;
			if (my < 1.0f) my = 1.0f;
			return true;
		}

		bool compute_viewport(Vec2i16& out, uintptr_t cam)
		{
			if (!cam) return false;

			const Vec3 world{
				g_write_x.load(std::memory_order_relaxed),
				g_write_y.load(std::memory_order_relaxed),
				g_write_z.load(std::memory_order_relaxed)
			};

			const Mat4 vm = rbx::view_matrix();
			const Vec2 size = rbx::viewport();
			if (size.x < 1.0f || size.y < 1.0f) return false;

			Vec2 s{};
			if (!rbx::w2s(world, s, vm, size.x, size.y)) return false;

			float mx = 0.0f, my = 0.0f;
			if (!mouse_in_viewport(overlay::target, size.x, size.y, mx, my)) return false;

			out = calc_viewport(s.x, s.y, size.x, size.y, mx, my);
			return true;
		}

		DWORD WINAPI writer_thread(LPVOID)
		{
			SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

			bool viewport_on = false;
			while (!g_write_stop.load(std::memory_order_acquire))
			{
				if (mem::hProc == NULL) break;

				// resolve the camera from the live datamodel every tick: the
				// 30Hz cached rbx::camera goes stale during a match-end teardown
				// and per-ms viewport writes into the freed object would land in
				// recycled heap memory and corrupt/crash the game.
				const uintptr_t cam = rbx::fresh_camera();

				if (g_write_active.load(std::memory_order_acquire))
				{
					Vec2i16 v{};
					if (cam && compute_viewport(v, cam))
					{
						g_last = v;
						g_fails = 0;
						viewport_on = true;
						write_viewport(cam, g_last);
					}
					else if (viewport_on && g_fails < 40)
					{
						++g_fails;
						if (cam)
							write_viewport(cam, g_last);
					}
					else if (viewport_on)
					{
						if (cam)
							restore_viewport(cam);
						else
							restore_viewport(rbx::camera);
						g_write_spoofed.store(false, std::memory_order_release);
						viewport_on = false;
						g_fails = 0;
					}
				}
				else if (viewport_on)
				{
					if (cam)
						restore_viewport(cam);
					else
						restore_viewport(rbx::camera);
					g_write_spoofed.store(false, std::memory_order_release);
					viewport_on = false;
					g_fails = 0;
				}

				if (g_rot_active.load(std::memory_order_acquire))
				{
					if (cam)
						write_rotation(cam);
				}

				Sleep(1);
			}

			restore_viewport(rbx::camera);
			g_write_spoofed.store(false, std::memory_order_release);
			return 0;
		}

		void ensure_thread()
		{
			if (g_thread) return;
			g_write_stop.store(false, std::memory_order_release);
			g_thread = CreateThread(nullptr, 0, &writer_thread, nullptr, 0, nullptr);
			if (g_thread) SetThreadPriority(g_thread, THREAD_PRIORITY_HIGHEST);
		}

		void set_silent_target(const Vec3& world)
		{
			ensure_thread();
			g_write_x.store(world.x, std::memory_order_relaxed);
			g_write_y.store(world.y, std::memory_order_relaxed);
			g_write_z.store(world.z, std::memory_order_relaxed);
			g_write_active.store(true, std::memory_order_release);
		}

		void clear_silent_target()
		{
			g_write_active.store(false, std::memory_order_release);
		}

		bool look_rotation(const Vec3& eye, const Vec3& target, Mat3& out)
		{
			float dx = target.x - eye.x;
			float dy = target.y - eye.y;
			float dz = target.z - eye.z;

			const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
			if (len < 0.0001f) return false;

			dx /= len;
			dy /= len;
			dz /= len;

			const float zx = -dx;
			const float zy = -dy;
			const float zz = -dz;

			float xx = zz;
			float xy = 0.0f;
			float xz = -zx;

			const float xlen = std::sqrt(xx * xx + xy * xy + xz * xz);
			if (xlen < 0.0001f)
			{
				xx = 1.0f;
				xy = 0.0f;
				xz = 0.0f;
			}
			else
			{
				xx /= xlen;
				xy /= xlen;
				xz /= xlen;
			}

			const float yx = zy * xz - zz * xy;
			const float yy = zz * xx - zx * xz;
			const float yz = zx * xy - zy * xx;

			// Roblox stores the CFrame rotation row-major as the components of
			// each basis vector: R00=(right.x,up.x,back.x), R10=(right.y,...), etc.
			out.m[0] = xx; out.m[1] = yx; out.m[2] = zx;
			out.m[3] = xy; out.m[4] = yy; out.m[5] = zy;
			out.m[6] = xz; out.m[7] = yz; out.m[8] = zz;
			return true;
		}

		void set_rotation_target(const Mat3& rot)
		{
			ensure_thread();
			{
				std::lock_guard<std::mutex> lock(g_rot_mutex);
				g_rot = rot;
			}
			g_rot_active.store(true, std::memory_order_release);
		}

		void clear_rotation_target()
		{
			g_rot_active.store(false, std::memory_order_release);
		}

		void set_raycast_target(bool on, const Vec3& world, bool wallbang = false)
		{
			if (on)
			{
				RaycastSilent::Ensure(true);
				if (RaycastSilent::Ready())
				{
					RaycastSilent::SetActive(true, world, wallbang);
				}
			}
			else
			{
				RaycastSilent::SetActive(false, Vec3{}, false);
				RaycastSilent::Ensure(false);
			}
		}
	}

	std::vector<std::string> body_part_labels()
	{
		return { "Head", "Root", "Torso", "Nearest",
		         "Left Arm", "Right Arm", "Left Leg", "Right Leg" };
	}

	void update(std::vector<rbx::Player>& players)
	{
		std::lock_guard<std::mutex> lock(g_mutex);

		struct State
		{
			bool toggle{ false };
			bool was{ false };
		};

		auto wants_for = [](bool is_enabled, bool menu, int bind, int use_mode, State& st)
		{
			if (!is_enabled || menu)
			{
				st.was = key_down(bind);
				st.toggle = false;
				return false;
			}

			if (use_mode == 2 || bind == 0) return true;

			if (check::blocked()) return false;

			const bool down = key_down(bind);
			if (use_mode == 1)
			{
				if (down && !st.was) st.toggle = !st.toggle;
				st.was = down;
				return st.toggle;
			}

			st.was = down;
			return down;
		};

		const bool menu_open = overlay::menu_open;
		const bool focused = roblox_focused();

		static State aim_st{};
		static State silent_st{};

		const bool wants = wants_for(enabled, menu_open, key, mode, aim_st);
		const bool silent_wants = wants_for(silent, menu_open, silent_key, silent_mode, silent_st);
		const bool magic_wants = magic_bullet && !menu_open;

		active = wants && focused;
		silent_active = (silent_wants || magic_wants) && focused;
		target_found = false;
		target_name.clear();
		s_tracer_valid = false;
		s_tracer_pos = {};

		if (!active && !silent_active)
		{
			clear_silent_target();
			clear_rotation_target();
			set_raycast_target(false, Vec3{});
			return;
		}

		const Mat4 vm = rbx::view_matrix();
		const Vec2 size = rbx::viewport();
		if (size.x < 1.0f || size.y < 1.0f) return;

		const float cx = size.x * 0.5f;
		const float cy = size.y * 0.5f;

		rbx::Player* aim_best = nullptr;
		Vec3 aim_pt{};
		float aim_dist = fov;
		Vec2 aim_screen{};

		rbx::Player* sil_best = nullptr;
		Vec3 sil_pt{};
		float sil_dist = silent_active ? silent_fov : 0.0f;
		Vec2 sil_screen{};

		for (auto& p : players)
		{
			if (!p.addr || p.addr == rbx::local_player) continue;
			if (!p.character || !p.humanoid) continue;
			if (sv::dead_check && (p.health <= 0.0f || p.max_health <= 0.0f)) continue;
			if (sv::team_check && p.friendly) continue;

			if (active)
			{
				Vec3 point = aim_point(p, aimpart, vm, size, g_dist);
				if (point.x != 0.0f || point.y != 0.0f || point.z != 0.0f)
				{
					Vec2 screen{};
					if (screen_of(point, vm, size, screen))
					{
						const float dx = screen.x - cx;
						const float dy = screen.y - cy;
						const float dist = std::sqrt(dx * dx + dy * dy);
						if (dist < aim_dist)
						{
							aim_best = &p;
							aim_pt = point;
							aim_dist = dist;
							aim_screen = screen;
						}
					}
				}
			}

			if (silent_active)
			{
				Vec3 point = aim_point(p, silentpart, vm, size, g_dist);
				if (point.x != 0.0f || point.y != 0.0f || point.z != 0.0f)
				{
					Vec2 screen{};
					if (screen_of(point, vm, size, screen))
					{
						const float dx = screen.x - cx;
						const float dy = screen.y - cy;
						const float dist = std::sqrt(dx * dx + dy * dy);
						if (dist < sil_dist)
						{
							sil_best = &p;
							sil_pt = point;
							sil_dist = dist;
							sil_screen = screen;
						}
					}
				}
			}
		}

		const Vec3 cam_pos = rbx::camera ? mem::read<Vec3>(rbx::camera + off::CameraPos) : Vec3{};

		Vec3 final_point{};
		if (aim_best)
		{
			const Vec3 delta = aim_pt - cam_pos;
			g_dist = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
			final_point = predict_point(*aim_best, aim_pt, pred_speed);
		}

		Vec3 final_sil{};
		if (sil_best)
		{
			const Vec3 delta = sil_pt - cam_pos;
			g_dist = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
			final_sil = predict_point(*sil_best, sil_pt, pred_speed);
		}

		const bool sil_ray = (silent_method == 1) || magic_bullet;

		if (silent_active && sil_best && sil_ray)
		{
			clear_silent_target();
			set_raycast_target(true, final_sil, magic_bullet);
		}
		else
		{
			set_raycast_target(false, Vec3{});
			if (silent_active && sil_best)
			{
				set_silent_target(final_sil);
			}
			else
			{
				clear_silent_target();
			}
		}

		if (active && aim_best && method == 1)
		{
			Mat3 rot{};
			if (look_rotation(cam_pos, final_point, rot))
			{
				set_rotation_target(rot);
			}
		}
		else
		{
			clear_rotation_target();
		}

		if (active && aim_best && method == 0 && !silent_active)
		{
			Vec2 screen{};
			if (screen_of(final_point, vm, size, screen))
			{
				const float smoothing = smooth < 1.0f ? 1.0f : smooth;
				const int dx = static_cast<int>(std::lround((screen.x - cx) / smoothing));
				const int dy = static_cast<int>(std::lround((screen.y - cy) / smoothing));
				move_mouse(dx, dy);
			}
		}

		if (auto_fire && active && aim_best && !silent_active && aim_dist <= auto_fire_fov)
		{
			const float now = static_cast<float>(GetTickCount64());
			if (now - last_fire_time >= auto_fire_delay)
			{
				last_fire_time = now;
				fire_click();
			}
		}

		target_found = aim_best != nullptr || (silent_active && sil_best != nullptr);
		if (silent_active && sil_best)
		{
			target_name = sil_best->display.empty() ? sil_best->name : sil_best->display;
			s_tracer_pos = sil_screen;
			s_tracer_valid = true;
		}
		else if (aim_best)
		{
			target_name = aim_best->display.empty() ? aim_best->name : aim_best->display;
			s_tracer_pos = aim_screen;
			s_tracer_valid = true;
		}
	}

	void draw()
	{
		std::lock_guard<std::mutex> lock(g_mutex);

		const Vec2 size = rbx::viewport();
		if (size.x < 1.0f || size.y < 1.0f) return;

		const float cx = size.x * 0.5f;
		const float cy = size.y * 0.5f;
		auto* dl = ImGui::GetBackgroundDrawList();

		if (draw_fov)
		{
			dl->AddCircle(ImVec2(cx, cy), fov, (ImU32)fov_color, 96, 1.5f);
		}

		if (draw_silent_fov)
		{
			dl->AddCircle(ImVec2(cx, cy), silent_fov, (ImU32)silent_fov_color, 96, 1.5f);
		}

		if (tracer && s_tracer_valid)
		{
			dl->AddLine(ImVec2(cx, cy), ImVec2(s_tracer_pos.x, s_tracer_pos.y), (ImU32)tracer_color, 1.5f);
		}
	}

	void shutdown()
	{
		clear_silent_target();
		clear_rotation_target();
		RaycastSilent::SetActive(false, Vec3{}, false);
		RaycastSilent::Ensure(false);

		if (!g_thread) return;

		g_write_stop.store(true, std::memory_order_release);
		WaitForSingleObject(g_thread, 1000);
		CloseHandle(g_thread);
		g_thread = nullptr;
	}
}
