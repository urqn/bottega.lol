#include "offsets.h"

#include <windows.h>
#include <winhttp.h>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace {

struct Leaf {
    std::string path;
    std::string value;
    bool is_number;
};

bool is_ws(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

bool is_ident(char c) { return std::isalnum((unsigned char)c) || c == '_'; }

std::string lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

std::string trim(std::string s) {
    size_t a = 0, b = s.size();
    while (a < b && is_ws(s[a])) ++a;
    while (b > a && is_ws(s[b - 1])) --b;
    return s.substr(a, b - a);
}

size_t skip_ws(const std::string& s, size_t i) {
    while (i < s.size() && is_ws(s[i])) ++i;
    return i;
}

std::string parse_string(const std::string& s, size_t& i, bool& ok) {
    std::string out;
    ok = false;
    if (i >= s.size() || s[i] != '"') return out;
    ++i;
    while (i < s.size()) {
        char c = s[i++];
        if (c == '"') { ok = true; break; }
        if (c != '\\' || i >= s.size()) { out.push_back(c); continue; }
        char e = s[i++];
        switch (e) {
            case 'n': out.push_back('\n'); break;
            case 't': out.push_back('\t'); break;
            case 'r': out.push_back('\r'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'u': {
                unsigned cp = 0;
                for (int k = 0; k < 4 && i < s.size(); ++k, ++i) {
                    char h = s[i];
                    cp <<= 4;
                    if (h >= '0' && h <= '9') cp |= (unsigned)(h - '0');
                    else if (h >= 'a' && h <= 'f') cp |= (unsigned)(h - 'a' + 10);
                    else if (h >= 'A' && h <= 'F') cp |= (unsigned)(h - 'A' + 10);
                }
                out.push_back(cp < 128 ? (char)cp : '?');
                break;
            }
            default: out.push_back(e); break;
        }
    }
    return out;
}

void parse_value(const std::string& s, size_t& i, const std::string& prefix, std::vector<Leaf>& out);

void parse_object(const std::string& s, size_t& i, const std::string& prefix, std::vector<Leaf>& out) {
    ++i;
    while (i < s.size()) {
        i = skip_ws(s, i);
        if (i >= s.size() || s[i] == '}') { if (i < s.size()) ++i; return; }
        if (s[i] == ',') { ++i; continue; }

        bool ok = false;
        std::string key = parse_string(s, i, ok);
        if (!ok) { ++i; continue; }

        i = skip_ws(s, i);
        if (i >= s.size() || s[i] != ':') continue;
        ++i;
        parse_value(s, i, prefix.empty() ? key : prefix + "." + key, out);
    }
}

void parse_value(const std::string& s, size_t& i, const std::string& prefix, std::vector<Leaf>& out) {
    i = skip_ws(s, i);
    if (i >= s.size()) return;

    const char c = s[i];
    if (c == '{') { parse_object(s, i, prefix, out); return; }

    if (c == '"') {
        bool ok = false;
        std::string v = parse_string(s, i, ok);
        if (ok) out.push_back({prefix, v, false});
        return;
    }

    if (c == '[') {
        ++i;
        int idx = 0;
        while (i < s.size()) {
            i = skip_ws(s, i);
            if (i >= s.size() || s[i] == ']') { if (i < s.size()) ++i; return; }
            if (s[i] == ',') { ++i; continue; }
            parse_value(s, i, prefix + "[" + std::to_string(idx++) + "]", out);
        }
        return;
    }

    const size_t start = i;
    while (i < s.size() && s[i] != ',' && s[i] != '}' && s[i] != ']' && !is_ws(s[i])) ++i;
    if (i > start) out.push_back({prefix, s.substr(start, i - start), true});
}

// understands a published C++ header: namespace Group { X = 0x..; }
std::vector<Leaf> parse_header(const std::string& body) {
    std::vector<Leaf> out;
    std::string ns;
    std::istringstream in(body);
    std::string line;

    while (std::getline(in, line)) {
        const size_t ns_pos = line.find("namespace ");
        if (ns_pos != std::string::npos) {
            size_t j = ns_pos + 10;
            while (j < line.size() && is_ws(line[j])) ++j;
            std::string id;
            while (j < line.size() && is_ident(line[j])) id.push_back(line[j++]);
            if (!id.empty()) ns = id;
            continue;
        }

        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = trim(line.substr(0, eq));
        size_t k = key.size();
        while (k > 0 && is_ident(key[k - 1])) --k;
        key = key.substr(k);
        if (key.empty()) continue;

        std::string val = line.substr(eq + 1);
        size_t cut = val.size();
        for (char stop : { ';', ',', '/' }) {
            const size_t at = val.find(stop);
            if (at != std::string::npos && at < cut) cut = at;
        }
        val = trim(val.substr(0, cut));
        if (val.empty()) continue;

        const std::string path = ns.empty() ? key : ns + "." + key;
        if (val.size() >= 2 && val.front() == '"' && val.back() == '"')
            out.push_back({path, val.substr(1, val.size() - 2), false});
        else
            out.push_back({path, val, true});
    }
    return out;
}

bool to_uptr(const std::string& tok, uintptr_t& out) {
    if (tok.empty()) return false;
    try {
        size_t used = 0;
        unsigned long long v = 0;
        if (tok.size() > 2 && tok[0] == '0' && (tok[1] == 'x' || tok[1] == 'X'))
            v = std::stoull(tok.substr(2), &used, 16);
        else
            v = std::stoull(tok, &used, 10);
        if (!used) return false;
        out = (uintptr_t)v;
        return true;
    }
    catch (...) {
        return false;
    }
}

std::string exe_dir() {
    char buf[MAX_PATH]{};
    const DWORD n = GetModuleFileNameA(NULL, buf, MAX_PATH);
    if (!n) return {};
    const std::string p(buf, n);
    const size_t slash = p.find_last_of("\\/");
    return slash == std::string::npos ? std::string() : p.substr(0, slash + 1);
}

std::string host() {
    char env[512]{};
    const DWORD n = GetEnvironmentVariableA("BOTTEGA_HOST", env, (DWORD)sizeof(env));
    if (n > 0 && n < sizeof(env)) {
        const std::string h = trim(std::string(env, n));
        if (!h.empty()) return h;
    }

    const std::string dir = exe_dir();
    if (!dir.empty()) {
        std::ifstream in(dir + "bottega.host");
        std::string line;
        while (in && std::getline(in, line)) {
            line = trim(line);
            if (line.empty() || line[0] == '#') continue;
            return line;
        }
    }
    return "bottega-lol-offsets.vercel.app";
}

struct Url {
    std::wstring host;
    std::wstring prefix;
    unsigned short port = INTERNET_DEFAULT_HTTPS_PORT;
    bool secure = true;
};

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)(n > 0 ? n : 0), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}

Url base_url() {
    std::string h = host();
    while (!h.empty() && (h.back() == '/' || h.back() == ' ')) h.pop_back();

    Url u;
    if (h.rfind("http://", 0) == 0) { u.secure = false; h = h.substr(7); }
    else if (h.rfind("https://", 0) == 0) { u.secure = true; h = h.substr(8); }

    const size_t slash = h.find('/');
    const std::string authority = slash == std::string::npos ? h : h.substr(0, slash);
    const std::string path = slash == std::string::npos ? std::string() : h.substr(slash);

    std::string hostname = authority;
    const size_t colon = authority.find(':');
    if (colon != std::string::npos) {
        hostname = authority.substr(0, colon);
        try { u.port = (unsigned short)std::stoi(authority.substr(colon + 1)); } catch (...) {}
    }
    else if (!u.secure) {
        u.port = INTERNET_DEFAULT_HTTP_PORT;
    }

    u.host = widen(hostname);
    u.prefix = widen(path);
    return u;
}

std::string http_get(const Url& base, const std::string& path) {
    std::string out;

    HINTERNET session = WinHttpOpen(L"bottega.lol/offsets",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return out;

    HINTERNET conn = WinHttpConnect(session, base.host.c_str(), base.port, 0);
    if (!conn) { WinHttpCloseHandle(session); return out; }

    const std::wstring full = base.prefix + widen(path);
    HINTERNET req = WinHttpOpenRequest(conn, L"GET", full.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        base.secure ? WINHTTP_FLAG_SECURE : 0);

    if (req) {
        DWORD timeout = 8000;
        WinHttpSetOption(req, WINHTTP_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
        WinHttpSetOption(req, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

        BOOL sent = WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0);

        if (sent && WinHttpReceiveResponse(req, nullptr)) {
            DWORD status = 0, size = sizeof(status);
            WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);

            if (status == 200) {
                DWORD avail = 0;
                while (WinHttpQueryDataAvailable(req, &avail) && avail > 0) {
                    std::vector<char> buf(avail);
                    DWORD read = 0;
                    if (!WinHttpReadData(req, buf.data(), avail, &read)) break;
                    out.append(buf.data(), read);
                }
            }
        }
        WinHttpCloseHandle(req);
    }

    WinHttpCloseHandle(conn);
    WinHttpCloseHandle(session);
    return out;
}

std::string cache_path() { return exe_dir() + "offsets.json"; }
std::string version_path() { return exe_dir() + "offsets.version"; }

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool write_file(const std::string& path, const std::string& body) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(body.data(), (std::streamsize)body.size());
    return (bool)out;
}

std::vector<Leaf> leaves_for(const std::string& body) {
    const std::string t = trim(body);
    if (!t.empty() && t[0] == '{') {
        std::vector<Leaf> out;
        size_t i = 0;
        parse_value(body, i, "", out);
        return out;
    }
    return parse_header(body);
}

uintptr_t pick(const std::map<std::string, uintptr_t>& flat, std::initializer_list<const char*> paths) {
    for (const char* p : paths) {
        const auto it = flat.find(lower(p));
        if (it != flat.end() && it->second) return it->second;
    }
    return 0;
}

bool apply_leaves(const std::vector<Leaf>& leaves, const std::string& client_version) {
    if (leaves.empty()) return false;

    std::map<std::string, uintptr_t> flat;
    std::map<std::string, std::string> strings;
    std::map<std::string, uintptr_t> all;

    for (const auto& l : leaves) {
        if (l.is_number) {
            uintptr_t v = 0;
            if (!to_uptr(l.value, v)) continue;

            // index the path plus every suffix past a dot so wrappers
            // ("offsets", "data", ...) do not hide the group names
            for (size_t dot = l.path.find('.'); ; dot = l.path.find('.', dot + 1)) {
                const std::string key = (dot == std::string::npos) ? l.path : l.path.substr(dot + 1);
                flat[lower(key)] = v;
                if (dot == std::string::npos) break;
            }

            const size_t dot = l.path.find('.');
            all[dot == std::string::npos ? l.path : l.path.substr(dot + 1)] = v;
        }
        else {
            strings[lower(l.path)] = l.value;
        }
    }
    if (all.empty()) return false;

    off::map = all;
    off::version = client_version;

    for (const auto& kv : strings) {
        const size_t dot = kv.first.find_last_of('.');
        const std::string name = (dot == std::string::npos) ? kv.first : kv.first.substr(dot + 1);
        if ((name == "roblox_version" || name == "clientversion" || name == "version") && !kv.second.empty()) {
            off::version = kv.second;
            break;
        }
    }

    off::FakeDataModelPtr = pick(flat, { "fakedatamodel.pointer", "fakedatamodelptr", "fakedatamodel.address" });
    off::FakeToReal       = pick(flat, { "fakedatamodel.realdatamodel", "fakedatamodel.real", "faketoreal" });
    off::VisualEnginePtr  = pick(flat, { "visualengine.pointer", "visualengineptr", "visualengine.address" });
    off::ViewMatrix       = pick(flat, { "visualengine.viewmatrix", "viewmatrix" });
    off::ViewportSz       = pick(flat, { "visualengine.dimensions", "visualengine.viewportsize", "visualengine.viewport" });
    off::Camera           = pick(flat, { "workspace.currentcamera", "workspace.camera" });
    off::CameraPos        = pick(flat, { "camera.position" });
    off::CameraViewport   = pick(flat, { "camera.viewport", "camera.viewportsize" });
    off::CameraRotation   = pick(flat, { "camera.rotation", "camera.cframe" });
    off::LocalPlayer      = pick(flat, { "players.localplayer", "player.localplayer" });
    off::NameContainer    = pick(flat, { "instance.namecontainer" });
    off::Name             = pick(flat, { "instance.name" });
    off::ClassDesc        = pick(flat, { "instance.classdescriptor", "instance.classdesc" });
    off::ClassName        = pick(flat, { "classdescriptor.classname" });
    off::Children         = pick(flat, { "instance.childrenstart", "instance.children" });
    off::ChildrenEnd      = pick(flat, { "instance.childrenend" });
    off::TeamColor        = pick(flat, { "player.teamcolor" });
    off::PlayerDisplayName = pick(flat, { "player.displayname" });
    off::ModelInstance    = pick(flat, { "player.character" });
    off::Health           = pick(flat, { "humanoid.health" });
    off::MaxHealth        = pick(flat, { "humanoid.maxhealth" });
    off::Primitive        = pick(flat, { "basepart.primitive", "basepart.partptr" });
    off::Position         = pick(flat, { "primitive.position" });
    off::Velocity         = pick(flat, { "primitive.assemblylinearvelocity", "basepart.assemblylinearvelocity" });
    off::Rotation         = pick(flat, { "primitive.rotation", "primitive.cframe" });
    off::RigType          = pick(flat, { "humanoid.rigtype" });
    off::Size             = pick(flat, { "primitive.size", "basepart.size" });
    return true;
}

void report(const char* name, uintptr_t value, std::string& missing) {
    if (value) return;
    if (!missing.empty()) missing += ", ";
    missing += name;
}

std::string important_missing() {
    std::string missing;
    report("FakeDataModelPtr", off::FakeDataModelPtr, missing);
    report("FakeToReal", off::FakeToReal, missing);
    report("VisualEnginePtr", off::VisualEnginePtr, missing);
    report("ViewMatrix", off::ViewMatrix, missing);
    report("ViewportSz", off::ViewportSz, missing);
    report("Camera", off::Camera, missing);
    report("CameraViewport", off::CameraViewport, missing);
    report("LocalPlayer", off::LocalPlayer, missing);
    report("NameContainer", off::NameContainer, missing);
    report("ClassDesc", off::ClassDesc, missing);
    report("ClassName", off::ClassName, missing);
    report("Children", off::Children, missing);
    report("ChildrenEnd", off::ChildrenEnd, missing);
    report("Primitive", off::Primitive, missing);
    report("Position", off::Position, missing);
    return missing;
}

} // namespace

bool off::fetch(const std::string& client_version) {
    const Url base = base_url();

    const std::string live = trim(http_get(base, "/roblox/version"));
    const std::string cached_version = trim(read_file(version_path()));
    std::string body = read_file(cache_path());

    if (!live.empty()) {
        const bool stale = (live != cached_version) || body.empty();
        printf("[*] offsets : live %s%s\n", live.c_str(),
            body.empty() ? "" : (stale ? " (update available)" : " (cached)"));
    }
    else {
        printf("[!] offsets : %s unreachable - using cache\n", host().c_str());
    }

    if (!live.empty() && ((live != cached_version) || body.empty())) {
        const std::string fresh = http_get(base, "/offsets.json");
        if (fresh.empty()) {
            printf("[-] offsets : download failed\n");
        }
        else if (!write_file(cache_path(), fresh)) {
            printf("[-] offsets : cannot write %s\n", cache_path().c_str());
            body = fresh;
        }
        else {
            write_file(version_path(), live);
            body = fresh;
            printf("[+] offsets : downloaded %zu bytes\n", fresh.size());
        }
    }

    if (body.empty()) {
        printf("[-] offsets : no offsets.json next to the exe\n");
        return false;
    }

    if (!apply_leaves(leaves_for(body), client_version)) {
        printf("[-] offsets : could not read %s\n", cache_path().c_str());
        return false;
    }

    if (!off::version.empty() && !client_version.empty() && off::version != client_version)
        printf("[!] offsets : version mismatch (client %s / offsets %s)\n",
            client_version.c_str(), off::version.c_str());

    const std::string missing = important_missing();
    if (!missing.empty()) {
        printf("[!] offsets : missing %s\n", missing.c_str());
        printf("[!] offsets : publish offsets.h / offsets.json for this build on %s\n", host().c_str());
        return false;
    }

    printf("[+] offsets : %zu entries for %s\n", off::map.size(), off::version.c_str());
    return true;
}
