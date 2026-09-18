#include "rbx.h"
#include "mem.h"
#include "offsets.h"
#include <unordered_map>
#include <mutex>


namespace rbx {

static std::unordered_map<uintptr_t, std::vector<uintptr_t>> kid_cache;
static std::unordered_map<uintptr_t, std::string>            name_cache;
static std::unordered_map<uintptr_t, std::string>            class_cache;

// the scan thread builds/clears these caches while the render thread reads them
// (esp/movement/aim lookups), so every cache touch happens under one mutex.
static std::mutex                                            cache_mutex;

static std::vector<uintptr_t> kids(uintptr_t inst);
static std::string            cname(uintptr_t inst);
static std::string            cclass(uintptr_t inst);


bool refresh()
{
    {
        std::lock_guard<std::mutex> lk(cache_mutex);
        kid_cache.clear();
        name_cache.clear();
        class_cache.clear();
    }

    if (!mem::base || !off::FakeDataModelPtr) return false;
    uintptr_t fdm = mem::read<uintptr_t>(mem::base + off::FakeDataModelPtr);
    if (!fdm) return false;

    datamodel = mem::read<uintptr_t>(fdm + off::FakeToReal);
    if (!datamodel) return false;

    workspace = find_child_byclass(datamodel, "Workspace");
    if (workspace) camera = mem::read<uintptr_t>(workspace + off::Camera);

    uintptr_t players_svc = find_child_byclass(datamodel, "Players");
    if (players_svc) local_player = mem::read<uintptr_t>(players_svc + off::LocalPlayer);


    if (off::VisualEnginePtr) {
        uintptr_t fresh = mem::read<uintptr_t>(mem::base + off::VisualEnginePtr);
        if (fresh) visual_eng = fresh;
    }
    return true;
}


static std::vector<uintptr_t> kids(uintptr_t inst) {
    std::lock_guard<std::mutex> lk(cache_mutex);
    auto it = kid_cache.find(inst);
    if (it != kid_cache.end()) return it->second;
    auto& slot = kid_cache[inst] = children(inst);
    return slot;
}

static std::string cname(uintptr_t inst) {
    std::lock_guard<std::mutex> lk(cache_mutex);
    auto it = name_cache.find(inst);
    if (it != name_cache.end()) return it->second;
    auto& slot = name_cache[inst] = name_of(inst);
    return slot;
}

static std::string cclass(uintptr_t inst)
{
    std::lock_guard<std::mutex> lk(cache_mutex);
    auto it = class_cache.find(inst);
    if (it != class_cache.end()) return it->second;
    auto& slot = class_cache[inst] = classname(inst);
    return slot;
}


std::string name_of(uintptr_t inst) {
    if (!inst) return {};
    // New layout: Instance -> NameContainer (ptr) -> +Name = std::string (inline)
    // Old layout: Instance -> +Name (ptr) -> std::string
    uintptr_t str = 0;
    if (off::NameContainer) {
        uintptr_t container = mem::read<uintptr_t>(inst + off::NameContainer);
        if (!container) return {};
        str = container + off::Name;
    } else {
        str = mem::read<uintptr_t>(inst + off::Name);
    }
    if (!str) return {};
    std::string s = mem::read_lenstr(str);
    if (s.empty()) s = mem::read_string(str);
    return s;
}

std::string classname(uintptr_t inst)
{
    if (!inst) return {};
    uintptr_t desc = mem::read<uintptr_t>(inst + off::ClassDesc);
    if (!desc) return {};
    uintptr_t cn = mem::read<uintptr_t>(desc + off::ClassName);
    if (!cn) return {};

    // ClassDescriptor::ClassName is a std::string* on this layout; older builds
    // exposed a raw char*, so accept whichever one reads back cleanly.
    std::string s = mem::read_lenstr(cn);
    if (s.empty()) s = mem::read_string(cn);
    return s;
}

std::vector<uintptr_t> children(uintptr_t inst) {
    std::vector<uintptr_t> out;
    if (!inst) return out;

    uintptr_t list = mem::read<uintptr_t>(inst + off::Children);
    if (!list) return out;
    uintptr_t start = mem::read<uintptr_t>(list);
    uintptr_t end   = mem::read<uintptr_t>(list + off::ChildrenEnd);
    if (!start || end <= start) return out;

    size_t span = end - start;
    if (span > 4096 * 16) return out;

    // one bulk read for the whole child slot range instead of one
    // ReadProcessMemory call per slot - this is the biggest syscall
    // saver in the 30Hz scan (Players service can have hundreds of slots).
    std::vector<uintptr_t> slot(span / sizeof(uintptr_t), 0);
    if (!mem::read_block(start, slot.data(), span))
        return out;

    out.reserve(span / 16);
    for (size_t i = 0; i + 1 < slot.size(); i += 2) { // entries are 16 bytes apart
        const uintptr_t c = slot[i];
        if (c) out.push_back(c);
    }
    return out;
}


uintptr_t find_child(uintptr_t inst, const char* name) {
    for (auto c : kids(inst))
        if (cname(c) == name) return c;
    return 0;
}
uintptr_t find_child_byclass(uintptr_t inst, const char* cls) {
    for (auto c : kids(inst))
        if (cclass(c) == cls) return c;
    return 0;
}


std::vector<Player> players()
{
    std::vector<Player> out;
    uintptr_t svc = find_child_byclass(datamodel, "Players");
    if (!svc) return out;

    // gather the raw player slots and read every team color on the first pass.
    std::vector<uintptr_t> addrs;
    for (auto p : kids(svc)) {
        if (cclass(p) == "Player") addrs.push_back(p);
    }
    if (addrs.empty()) return out;

    const int my_team = local_player ? mem::read<int>(local_player + off::TeamColor) : 0;

    std::vector<int> colors;
    colors.reserve(addrs.size() + 1);
    colors.push_back(my_team);
    for (uintptr_t p : addrs)
        colors.push_back(mem::read<int>(p + off::TeamColor));

    // A player not on a real team reads its neutral default TeamColor, so in a
    // free-for-all every entry is identical and the old `my_team != 0` guard is
    // useless - it flagged the whole lobby friendly and team check erased every
    // box. Only treat colors as meaningful when the lobby has at least two
    // distinct values (i.e. real teams exist); otherwise nobody is "friendly".
    int distinct = 0;
    for (int c : colors) {
        bool seen = false;
        for (int i = 0; i < distinct; ++i)
            if (colors[i] == c) { seen = true; break; }
        if (!seen) colors[distinct++] = c;
    }
    const bool has_teams = distinct > 1;

    for (size_t i = 0; i < addrs.size(); ++i) {
        uintptr_t p = addrs[i];
        Player pl{};
        pl.addr = p;
        pl.character = mem::read<uintptr_t>(p + off::ModelInstance);
        pl.name = cname(p);
        pl.friendly = has_teams && (colors[i + 1] == my_team);
        if (off::PlayerDisplayName) pl.display = mem::read_lenstr(p + off::PlayerDisplayName);

        if (pl.character) {
            pl.hrp = find_child(pl.character, "HumanoidRootPart");
            pl.humanoid = find_child_byclass(pl.character, "Humanoid");
            if (pl.humanoid) {
                pl.health = mem::read<float>(pl.humanoid + off::Health);
                pl.max_health = mem::read<float>(pl.humanoid + off::MaxHealth);
            }
            if (pl.hrp) {
                uintptr_t prim = mem::read<uintptr_t>(pl.hrp + off::Primitive);
                if (prim) pl.pos = mem::read<Vec3>(prim + off::Position);
            }
        }

        out.push_back(pl);
    }
    return out;
}

Mat4 view_matrix() {
    Mat4 m{};
    if (!visual_eng || !off::ViewMatrix) return m;
    return mem::read<Mat4>(visual_eng + off::ViewMatrix);
}


Vec2 viewport() {
    if (!visual_eng || !off::ViewportSz) return { 1920.f, 1080.f };
    return mem::read<Vec2>(visual_eng + off::ViewportSz);
}

bool w2s(const Vec3& w, Vec2& s, const Mat4& mv, float sw, float sh)
{
    float tw_ = mv.m[12] * w.x + mv.m[13] * w.y + mv.m[14] * w.z + mv.m[15];
    if (tw_ <= 0.001f) return false;

    float tx = mv.m[0]*w.x + mv.m[1]*w.y + mv.m[2]*w.z + mv.m[3];
    float ty = mv.m[4]*w.x + mv.m[5]*w.y + mv.m[6]*w.z + mv.m[7];

    float inv = 1.0f / tw_;
    s.x = (sw * 0.5f) * (tx * inv + 1.0f);
    s.y = (sh * 0.5f) * (1.0f - ty * inv);
    return true;
}

}
