#include "serverbrowser.h"

#include <Windows.h>
#include <winhttp.h>
#include <shellapi.h>
#pragma comment(lib, "winhttp.lib")

#include <algorithm>
#include <atomic>
#include <cctype>
#include <mutex>
#include <sstream>
#include <thread>

#include <nlohmann/json.hpp>

#include "rbx.h"
#include "toast.h"
#include "waypoints.h"

using json = nlohmann::json;

#include <cstring>
#include <cstdio>
#include <string>

namespace servers
{
	namespace
	{
		void copy_to_clipboard(const std::string& text)
		{
			if (!OpenClipboard(nullptr)) return;
			EmptyClipboard();
			HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
			if (h)
			{
				void* p = GlobalLock(h);
				if (p) std::memcpy(p, text.c_str(), text.size() + 1);
				GlobalUnlock(h);
				SetClipboardData(CF_TEXT, h);
			}
			CloseClipboard();
		}

		std::string safe_str(const json& j, const char* key)
		{
			if (j.contains(key) && j[key].is_string()) return j[key].get<std::string>();
			return "";
		}
		int safe_int(const json& j, const char* key)
		{
			if (j.contains(key) && j[key].is_number()) return j[key].get<int>();
			return 0;
		}
		float safe_float(const json& j, const char* key)
		{
			if (j.contains(key) && j[key].is_number()) return j[key].get<float>();
			return 0.0f;
		}
		std::uint64_t safe_u64(const json& j, const char* key)
		{
			if (j.contains(key) && j[key].is_number()) return j[key].get<std::uint64_t>();
			return 0;
		}

		std::string http_get(const std::wstring& host, const std::wstring& path)
		{
			std::string out;
			HINTERNET hSession = WinHttpOpen(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) Chrome/120.0.0.0 Safari/537.36",
				WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
			if (!hSession) return out;

			HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
			if (!hConnect)
			{
				WinHttpCloseHandle(hSession);
				return out;
			}

			HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(), NULL, WINHTTP_NO_REFERER,
				WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
			if (!hRequest)
			{
				WinHttpCloseHandle(hConnect);
				WinHttpCloseHandle(hSession);
				return out;
			}

			DWORD timeout = 10000;
			WinHttpSetOption(hRequest, WINHTTP_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
			WinHttpSetOption(hRequest, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

			const wchar_t* headers = L"Accept: application/json\r\n"
				L"User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) Chrome/120.0.0.0 Safari/537.36\r\n";
			WinHttpAddRequestHeaders(hRequest, headers, (DWORD)wcslen(headers), WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);

			if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
				WinHttpReceiveResponse(hRequest, NULL))
			{
				char buf[8192];
				DWORD got = 0;
				while (WinHttpReadData(hRequest, buf, sizeof(buf) - 1, &got) && got > 0)
				{
					buf[got] = '\0';
					out.append(buf, got);
				}
			}

			WinHttpCloseHandle(hRequest);
			WinHttpCloseHandle(hConnect);
			WinHttpCloseHandle(hSession);
			return out;
		}

		void set_state(const std::vector<Server>& all, const std::string& status, const std::string& next, const std::string& prev)
		{
			static std::mutex m;
			static std::vector<Server> g_all;
			static std::string g_status, g_next, g_prev;
			std::lock_guard<std::mutex> lk(m);
			g_all = all;
			g_status = status;
			g_next = next;
			g_prev = prev;
		}
		void get_state(std::vector<Server>& all, std::string& status, std::string& next, std::string& prev)
		{
			static std::mutex m;
			static std::vector<Server> g_all;
			static std::string g_status, g_next, g_prev;
			std::lock_guard<std::mutex> lk(m);
			all = g_all;
			status = g_status;
			next = g_next;
			prev = g_prev;
		}

		std::atomic<bool> g_loading{ false };
		int g_page = 0;
		std::string g_cursor_next, g_cursor_prev;
		std::vector<std::string> g_cursor_history;
	}

	bool is_loading() { return g_loading.load(); }

	std::string status()
	{
		std::vector<Server> a; std::string s, n, p;
		get_state(a, s, n, p);
		return s;
	}

	void set_place(std::uint64_t place)
	{
		place_id = place;
		std::string s = std::to_string(place);
		std::memcpy(input_place_id, s.c_str(), std::min<std::size_t>(s.size() + 1, sizeof(input_place_id) - 1));
	}

	std::uint64_t target_place()
	{
		std::string digits;
		for (char c : input_place_id)
		{
			if (c == '\0') break;
			if (std::isdigit((unsigned char)c)) digits += c;
		}
		if (!digits.empty())
		{
			try { return std::stoull(digits); } catch (...) {}
		}
		return place_id;
	}

	void refresh()
	{
		const std::uint64_t pid = target_place();
		if (pid == 0)
		{
			set_state({}, "Invalid place id.", "", "");
			return;
		}
		if (g_loading.load()) return;

		g_loading.store(true);
		g_page = 0;
		g_cursor_next.clear();
		g_cursor_prev.clear();
		g_cursor_history.clear();

		std::thread([pid]() {
			std::wstringstream wss;
			wss << L"/v1/games/" << pid << L"/servers/Public?limit=100";
			std::string res = http_get(L"games.roblox.com", wss.str());

			if (res.find("The place is invalid") != std::string::npos)
			{
				std::wstringstream uss;
				uss << L"/v1/games?universeIds=" << pid;
				std::string ures = http_get(L"games.roblox.com", uss.str());
				if (!ures.empty())
				{
					try
					{
						json root = json::parse(ures);
						if (root.contains("data") && root["data"].is_array() && !root["data"].empty())
						{
							std::uint64_t rp = safe_u64(root["data"][0], "rootPlaceId");
							if (rp > 0)
							{
								std::wstringstream nws;
								nws << L"/v1/games/" << rp << L"/servers/Public?limit=100";
								res = http_get(L"games.roblox.com", nws.str());
								set_place(rp);
							}
						}
					}
					catch (...) {}
				}
			}

			g_loading.store(false);

			if (res.empty())
			{
				set_state({}, "Failed to query servers (timeout / network).", "", "");
				return;
			}

			try
			{
				json root = json::parse(res);
				std::vector<Server> all;
				if (root.contains("data") && root["data"].is_array())
				{
					for (const auto& item : root["data"])
					{
						Server s;
						s.id = safe_str(item, "id");
						s.max_players = safe_int(item, "maxPlayers");
						s.playing = safe_int(item, "playing");
						s.ping = safe_int(item, "ping");
						s.fps = safe_float(item, "fps");
						if (!s.id.empty()) all.push_back(std::move(s));
					}
					g_cursor_next = safe_str(root, "nextPageCursor");
					g_cursor_prev = safe_str(root, "previousPageCursor");
				}
				set_state(all, "Found " + std::to_string(all.size()) + " servers", g_cursor_next, g_cursor_prev);
			}
			catch (const std::exception& e)
			{
				set_state({}, std::string("Parse error: ") + e.what(), "", "");
			}
		}).detach();
	}

	void next_page()
	{
		if (g_cursor_next.empty() || g_loading.load()) return;
		const std::uint64_t pid = target_place();
		if (pid == 0) return;

		g_loading.store(true);
		g_cursor_history.push_back(g_cursor_next);
		++g_page;

		std::thread([pid, cursor = g_cursor_next]() {
			std::wstringstream wss;
			wss << L"/v1/games/" << pid << L"/servers/Public?limit=100&cursor=" << std::wstring(cursor.begin(), cursor.end());
			std::string res = http_get(L"games.roblox.com", wss.str());
			g_loading.store(false);

			std::vector<Server> all;
			std::string nxt, prv;
			try
			{
				json root = json::parse(res);
				if (root.contains("data") && root["data"].is_array())
				{
					for (const auto& item : root["data"])
					{
						Server s;
						s.id = safe_str(item, "id");
						s.max_players = safe_int(item, "maxPlayers");
						s.playing = safe_int(item, "playing");
						s.ping = safe_int(item, "ping");
						s.fps = safe_float(item, "fps");
						if (!s.id.empty()) all.push_back(std::move(s));
					}
					nxt = safe_str(root, "nextPageCursor");
					prv = safe_str(root, "previousPageCursor");
				}
				g_cursor_next = nxt;
				g_cursor_prev = prv;
				set_state(all, "Found " + std::to_string(all.size()) + " servers", nxt, prv);
			}
			catch (...)
			{
				set_state({}, "Parse error.", "", "");
			}
		}).detach();
	}

	void prev_page()
	{
		if (g_page < 1 || g_cursor_history.empty() || g_loading.load()) return;
		g_cursor_history.pop_back();
		--g_page;
		std::string cursor = g_cursor_history.empty() ? "" : g_cursor_history.back();
		const std::uint64_t pid = target_place();
		if (pid == 0) return;

		g_loading.store(true);
		std::thread([pid, cursor]() {
			std::wstringstream wss;
			wss << L"/v1/games/" << pid << L"/servers/Public?limit=100";
			if (!cursor.empty()) wss << L"&cursor=" << std::wstring(cursor.begin(), cursor.end());
			std::string res = http_get(L"games.roblox.com", wss.str());
			g_loading.store(false);

			std::vector<Server> all;
			std::string nxt, prv;
			try
			{
				json root = json::parse(res);
				if (root.contains("data") && root["data"].is_array())
				{
					for (const auto& item : root["data"])
					{
						Server s;
						s.id = safe_str(item, "id");
						s.max_players = safe_int(item, "maxPlayers");
						s.playing = safe_int(item, "playing");
						s.ping = safe_int(item, "ping");
						s.fps = safe_float(item, "fps");
						if (!s.id.empty()) all.push_back(std::move(s));
					}
					nxt = safe_str(root, "nextPageCursor");
					prv = safe_str(root, "previousPageCursor");
				}
				g_cursor_next = nxt;
				g_cursor_prev = prv;
				set_state(all, "Found " + std::to_string(all.size()) + " servers", nxt, prv);
			}
			catch (...)
			{
				set_state({}, "Parse error.", "", "");
			}
		}).detach();
	}

	void join(const Server& s)
	{
		if (s.id.empty()) return;
		std::string uri = "roblox://experiences/start?placeId=" + std::to_string(target_place()) + "&gameInstanceId=" + s.id;
		HINSTANCE r = ShellExecuteA(NULL, "open", uri.c_str(), NULL, NULL, SW_SHOWNORMAL);
		if ((INT_PTR)r > 32) toast::push(toast::Kind::Success, "Connecting to server...");
		else toast::push(toast::Kind::Error, "Failed to launch Roblox URI");
	}

	void copy_job_id(const Server& s)
	{
		copy_to_clipboard(s.id);
		toast::push(toast::Kind::Success, "Job ID copied");
	}

	void copy_teleport_script(const Server& s)
	{
		std::string script = "game:GetService(\"TeleportService\"):TeleportToPlaceInstance(" +
			std::to_string(target_place()) + ", \"" + s.id + "\", game.Players.LocalPlayer)";
		copy_to_clipboard(script);
		toast::push(toast::Kind::Success, "Teleport script copied");
	}

	std::vector<Server> filtered()
	{
		std::vector<Server> a; std::string s, n, p;
		get_state(a, s, n, p);
		return a;
	}

	std::vector<Server> visible()
	{
		std::vector<Server> vis;
		for (const auto& s : filtered())
		{
			if (exclude_full && s.max_players > 0 && s.playing >= s.max_players) continue;
			vis.push_back(s);
		}
		switch (sort_mode)
		{
		case 1:
			std::sort(vis.begin(), vis.end(), [](const Server& a, const Server& b) {
				if (a.playing != b.playing) return a.playing > b.playing;
				return a.ping < b.ping; });
			break;
		case 2:
			std::sort(vis.begin(), vis.end(), [](const Server& a, const Server& b) {
				if (a.ping != b.ping) return a.ping < b.ping;
				return a.playing < b.playing; });
			break;
		case 3:
			std::sort(vis.begin(), vis.end(), [](const Server& a, const Server& b) { return a.fps > b.fps; });
			break;
		case 0:
		default:
			std::sort(vis.begin(), vis.end(), [](const Server& a, const Server& b) {
				if (a.playing != b.playing) return a.playing < b.playing;
				return a.ping < b.ping; });
			break;
		}
		return vis;
	}

	std::string row_label(const Server& s)
	{
		char buf[192];
		std::snprintf(buf, sizeof(buf), "%d/%d   %dms   ~%.0f fps", s.playing, s.max_players, s.ping, s.fps);
		return buf;
	}
} // namespace servers