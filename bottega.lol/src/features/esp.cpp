#include "esp.h"
#include "settings.h"
#include "mem.h"
#include "offsets.h"
#include "overlay.h"
#include "mesh_esp.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>


namespace esp {

static bool focused()
{
    HWND fg = GetForegroundWindow();
    return fg == overlay::target || fg == overlay::hwnd;
}

static ImFont* font()
{
    ImFont* f = ImGui::GetFont();
    return f ? f : ImGui::GetIO().FontDefault;
}

static ImU32 to_col(std::uint32_t c) { return static_cast<ImU32>(c); }

static bool valid(const Vec3& p) { return p.x != 0.f || p.y != 0.f || p.z != 0.f; }

// ── Limb cache ──────────────────────────────────────────────────────────────
struct Limbs {
    std::uintptr_t head{}, hrp{}, torso{}, upper_torso{}, lower_torso{};
    std::uintptr_t l_arm{}, r_arm{}, l_leg{}, r_leg{};
    std::uintptr_t l_upper_arm{}, l_lower_arm{}, l_hand{}, r_upper_arm{}, r_lower_arm{}, r_hand{};
    std::uintptr_t l_upper_leg{}, l_lower_leg{}, l_foot{}, r_upper_leg{}, r_lower_leg{}, r_foot{};
    std::string tool{};
    bool r6{};
    ULONGLONG stamp = 0;
};

static std::unordered_map<std::uintptr_t, Limbs> limb_cache;
static ULONGLONG g_now = 0;                        // ms clock, refreshed each draw pass
static constexpr ULONGLONG k_pos_fresh_ms  = 33;   // part geometry TTL (~30 Hz, fps-independent)
static constexpr ULONGLONG k_limb_fresh_ms = 600;  // limb re-scan TTL

// ── Part data cache ─────────────────────────────────────────────────────────
// All 9 rotation floats + pos + size are fetched in two bulk RPMs instead of
// 11 separate ReadProcessMemory calls (9× floats + pos + size previously).
struct PartData {
    ULONGLONG stamp = 0;
    std::uintptr_t prim = 0;
    Vec3 pos{};
    Vec3 sz{ 1.f, 1.f, 1.f };
    float rot[9]{ 1, 0, 0, 0, 1, 0, 0, 0, 1 };
    Vec3 vel{};      // cached once; only populated when flags/view_dir is on
    bool vel_valid = false;
};
static std::unordered_map<std::uintptr_t, PartData> part_cache;

// Primitive layout: Position(vec3 @+0) then immediately Size(vec3 @+12) then
// Rotation (9 floats @+24) — 60 bytes total read in one RPM.
// If your offsets layout is different, adjust k_prim_block_size.
static constexpr std::size_t k_prim_pos_off  = 0;
static constexpr std::size_t k_prim_sz_off   = 12;
static constexpr std::size_t k_prim_rot_off  = 24;
static constexpr std::size_t k_prim_block_sz = 24 + 9 * 4; // 60 bytes

static const PartData* part_data(std::uintptr_t part)
{
    if (!part || !off::Primitive || !off::Position) return nullptr;

    PartData& pd = part_cache[part];
    if (pd.stamp && g_now - pd.stamp < k_pos_fresh_ms)
        return &pd;

    pd.stamp     = g_now;
    pd.vel_valid = false;
    pd.prim = mem::read<std::uintptr_t>(part + off::Primitive);
    if (!pd.prim) return &pd;

    // Bulk-read pos + size + rot in one syscall if offsets are contiguous.
    // Falls back to individual reads if they're not adjacent.
    const bool contiguous = off::Size     == (off::Position + 12) &&
                            off::Rotation == (off::Position + 24);
    if (contiguous) {
        struct PrimBlock { float pos[3]; float sz[3]; float rot[9]; };
        PrimBlock blk{};
        if (mem::read_block(pd.prim + off::Position, &blk, sizeof(blk))) {
            pd.pos = Vec3{ blk.pos[0], blk.pos[1], blk.pos[2] };
            pd.sz  = Vec3{ blk.sz[0],  blk.sz[1],  blk.sz[2]  };
            std::memcpy(pd.rot, blk.rot, sizeof(pd.rot));
            return &pd;
        }
    }

    // Non-contiguous fallback: two RPMs (pos+size together if adjacent, else three)
    pd.pos = mem::read<Vec3>(pd.prim + off::Position);
    if (off::Size)
        pd.sz = mem::read<Vec3>(pd.prim + off::Size);
    if (off::Rotation) {
        // 9 floats in one RPM instead of 9 separate reads
        mem::read_block(pd.prim + off::Rotation, pd.rot, 9 * sizeof(float));
    }
    return &pd;
}

void invalidate()
{
    limb_cache.clear();
    part_cache.clear();
    g_now = 0;
}

static Vec3 part_pos(std::uintptr_t part)
{
    const PartData* pd = part_data(part);
    return pd ? pd->pos : Vec3{};
}

static Limbs limbs_of(std::uintptr_t character)
{
    if (!character)
        return Limbs{};

    Limbs& l = limb_cache[character];

    // Stale check: use stamp age only — no extra part_pos RPM every frame.
    const bool stale = (l.stamp == 0) || (g_now - l.stamp > k_limb_fresh_ms) ||
                       (!l.head || !l.hrp);
    if (!stale)
        return l;

    l = Limbs{};
    l.stamp = g_now;

    l.head = rbx::find_child(character, "Head");
    l.hrp  = rbx::find_child(character, "HumanoidRootPart");
    l.upper_torso = rbx::find_child(character, "UpperTorso");
    l.r6 = (l.upper_torso == 0);

    if (l.r6) {
        l.torso = rbx::find_child(character, "Torso");
        l.l_arm = rbx::find_child(character, "Left Arm");
        l.r_arm = rbx::find_child(character, "Right Arm");
        l.l_leg = rbx::find_child(character, "Left Leg");
        l.r_leg = rbx::find_child(character, "Right Leg");
    }
    else {
        l.lower_torso = rbx::find_child(character, "LowerTorso");
        l.l_upper_arm = rbx::find_child(character, "LeftUpperArm");
        l.l_lower_arm = rbx::find_child(character, "LeftLowerArm");
        l.l_hand      = rbx::find_child(character, "LeftHand");
        l.r_upper_arm = rbx::find_child(character, "RightUpperArm");
        l.r_lower_arm = rbx::find_child(character, "RightLowerArm");
        l.r_hand      = rbx::find_child(character, "RightHand");
        l.l_upper_leg = rbx::find_child(character, "LeftUpperLeg");
        l.l_lower_leg = rbx::find_child(character, "LeftLowerLeg");
        l.l_foot      = rbx::find_child(character, "LeftFoot");
        l.r_upper_leg = rbx::find_child(character, "RightUpperLeg");
        l.r_lower_leg = rbx::find_child(character, "RightLowerLeg");
        l.r_foot      = rbx::find_child(character, "RightFoot");
    }

    if (std::uintptr_t tool = rbx::find_child_byclass(character, "Tool"))
        l.tool = rbx::name_of(tool);

    return l;
}

// ── Draw helpers ─────────────────────────────────────────────────────────────

// outlined_text: drop shadow + main = 2 draws instead of a 5-pass halo; the
// cheap shadow is plenty for the small ESP labels and roughly halves the text
// vertex volume per player (adds up fast with many players / flags).
static void outlined_text(ImDrawList* dl, const ImVec2& pos, const char* text, ImU32 col)
{
    if (!text || !*text) return;
    ImFont* f = font();
    const ImU32 shadow = IM_COL32(0, 0, 0, 255);
    dl->AddText(f, font_size, ImVec2{ pos.x + 1.f, pos.y + 1.f }, shadow, text);
    dl->AddText(f, font_size, pos,                           col,    text);
}

static void outlined_text(ImDrawList* dl, const ImVec2& pos, const std::string& text, ImU32 col)
{
    outlined_text(dl, pos, text.c_str(), col);
}

static ImVec2 text_size(const char* text)
{
    if (!text || !*text) return ImVec2{ 0.f, font_size };
    return font()->CalcTextSizeA(font_size, FLT_MAX, 0.f, text);
}

static ImVec2 text_size(const std::string& text)
{
    return text_size(text.c_str());
}

static void line(ImDrawList* dl, const ImVec2& a, const ImVec2& b, ImU32 col, float t, bool outline)
{
    if (outline)
        dl->AddLine(a, b, IM_COL32(0, 0, 0, 255), t + 0.5f);
    dl->AddLine(a, b, col, t);
}

static bool w2s(const Vec3& w, const Mat4& mv, float sw, float sh, ImVec2& out)
{
    Vec2 s{};
    if (!rbx::w2s(w, s, mv, sw, sh)) return false;
    out = ImVec2{ s.x, s.y };
    return true;
}

static void bone(ImDrawList* dl, const Vec3& a, const Vec3& b, const Mat4& mv, float sw, float sh)
{
    ImVec2 sa{}, sb{};
    if (!w2s(a, mv, sw, sh, sa) || !w2s(b, mv, sw, sh, sb)) return;
    line(dl, sa, sb, to_col(skeleton_color), skeleton_thickness, skeleton_outline);
}

static void bone_addr(ImDrawList* dl, std::uintptr_t a, std::uintptr_t b, const Mat4& mv, float sw, float sh)
{
    if (!a || !b) return;
    const Vec3 pa = part_pos(a);
    const Vec3 pb = part_pos(b);
    if (!valid(pa) || !valid(pb)) return;
    bone(dl, pa, pb, mv, sw, sh);
}

static void render_skeleton(ImDrawList* dl, const Limbs& l, const Mat4& mv, float sw, float sh)
{
    if (l.r6) {
        if (!l.head || !l.torso) return;
        const Vec3 hp = part_pos(l.head);
        const Vec3 tp = part_pos(l.torso);
        bone(dl, hp, tp, mv, sw, sh);
        const std::uintptr_t parts[] = { l.l_arm, l.r_arm, l.l_leg, l.r_leg };
        for (auto p : parts) {
            if (!p) continue;
            bone(dl, tp, part_pos(p), mv, sw, sh);
        }
        return;
    }

    if (!l.head || !l.upper_torso) return;
    const Vec3 hp = part_pos(l.head);
    const Vec3 up = part_pos(l.upper_torso);
    bone(dl, hp, up, mv, sw, sh);

    if (!l.lower_torso) return;
    const Vec3 lp = part_pos(l.lower_torso);
    bone(dl, up, lp, mv, sw, sh);

    const std::uintptr_t left_arm[]  = { l.l_upper_arm, l.l_lower_arm, l.l_hand };
    const std::uintptr_t right_arm[] = { l.r_upper_arm, l.r_lower_arm, l.r_hand };
    const std::uintptr_t left_leg[]  = { l.l_upper_leg, l.l_lower_leg, l.l_foot };
    const std::uintptr_t right_leg[] = { l.r_upper_leg, l.r_lower_leg, l.r_foot };

    auto chain = [&](const std::uintptr_t* c) {
        for (int i = 1; i < 3; ++i) {
            if (!c[i - 1] || !c[i]) return;
            bone_addr(dl, c[i - 1], c[i], mv, sw, sh);
        }
    };

    bone_addr(dl, l.upper_torso, l.l_upper_arm, mv, sw, sh);
    bone_addr(dl, l.upper_torso, l.r_upper_arm, mv, sw, sh);
    chain(left_arm);
    chain(right_arm);

    bone_addr(dl, l.lower_torso, l.l_upper_leg, mv, sw, sh);
    bone_addr(dl, l.lower_torso, l.r_upper_leg, mv, sw, sh);
    chain(left_leg);
    chain(right_leg);
}

static void draw_box_fill(ImDrawList* dl, float x0, float y0, float x1, float y1)
{
    if (!box_filled) return;
    if (box_fill_gradient)
        dl->AddRectFilledMultiColor(ImVec2{ x0, y0 }, ImVec2{ x1, y1 },
            to_col(box_fill_color), to_col(box_fill_color), to_col(box_fill_color2), to_col(box_fill_color2));
    else
        dl->AddRectFilled(ImVec2{ x0, y0 }, ImVec2{ x1, y1 }, to_col(box_fill_color));
}

// ── Box drawing ──────────────────────────────────────────────────────────────

static void snap_box(float min_x, float min_y, float max_x, float max_y,
                     float& x1, float& y1, float& x2, float& y2)
{
    x1 = std::floor(min_x);
    y1 = std::floor(min_y);
    x2 = std::ceil(max_x);
    y2 = std::ceil(max_y);
    if (x2 <= x1) x2 = x1 + 1.0f;
    if (y2 <= y1) y2 = y1 + 1.0f;
}

static void draw_box_px(ImDrawList* dl, float x0, float y0, float x1, float y1, ImU32 col)
{
    float rx1, ry1, rx2, ry2;
    snap_box(x0, y0, x1, y1, rx1, ry1, rx2, ry2);
    const float t = (std::max)(box_thickness, 0.5f);

    if (box_outline) {
        if (t <= 1.01f) {
            dl->AddRect(ImVec2(rx1 - 1.f, ry1 - 1.f), ImVec2(rx2 + 1.f, ry2 + 1.f),
                IM_COL32(0, 0, 0, 255), 0.0f, 0, 1.0f);
            dl->AddRect(ImVec2(rx1 + 1.f, ry1 + 1.f), ImVec2(rx2 - 1.f, ry2 - 1.f),
                IM_COL32(0, 0, 0, 255), 0.0f, 0, 1.0f);
        } else {
            dl->AddRect(ImVec2(rx1, ry1), ImVec2(rx2, ry2),
                IM_COL32(0, 0, 0, 255), 0.0f, 0, t + 2.0f);
        }
    }
    dl->AddRect(ImVec2(rx1, ry1), ImVec2(rx2, ry2), col, 0.0f, 0, t);
}

static void draw_corner_box_px(ImDrawList* dl, float x0, float y0, float x1, float y1, ImU32 col)
{
    if (x1 <= x0 || y1 <= y0) return;

    float rx1, ry1, rx2, ry2;
    snap_box(x0, y0, x1, y1, rx1, ry1, rx2, ry2);
    const float t = (std::max)(box_thickness, 0.5f);

    float lw = std::floor((rx2 - rx1) * 0.25f);
    float lh = std::floor((ry2 - ry1) * 0.25f);
    if (lw < 2.f) lw = 2.f;
    if (lh < 2.f) lh = 2.f;

    struct Seg { ImVec2 a, b; };
    const Seg segs[8] = {
        { {rx1, ry1}, {rx1 + lw, ry1} }, { {rx1, ry1}, {rx1, ry1 + lh} },
        { {rx2 - lw, ry1}, {rx2, ry1} }, { {rx2, ry1}, {rx2, ry1 + lh} },
        { {rx1, ry2 - lh}, {rx1, ry2} }, { {rx1, ry2}, {rx1 + lw, ry2} },
        { {rx2, ry2 - lh}, {rx2, ry2} }, { {rx2 - lw, ry2}, {rx2, ry2} },
    };
    if (box_outline) {
        const float ot = t + 2.0f;
        for (const auto& s : segs) dl->AddLine(s.a, s.b, IM_COL32(0, 0, 0, 255), ot);
    }
    for (const auto& s : segs) dl->AddLine(s.a, s.b, col, t);
}

inline constexpr int k_box_edges[12][2] = {
    {0,1},{0,2},{0,4},{1,3},{1,5},{2,3},
    {2,6},{3,7},{4,5},{4,6},{5,7},{6,7}
};

static void draw_box_3d_edges(ImDrawList* dl, const ImVec2 pts[8], ImU32 col)
{
    const float t = (std::max)(box_thickness, 0.5f);
    if (box_outline) {
        const float ot = t + 2.0f;
        for (const auto& e : k_box_edges)
            dl->AddLine(pts[e[0]], pts[e[1]], IM_COL32(0, 0, 0, 255), ot);
    }
    for (const auto& e : k_box_edges)
        dl->AddLine(pts[e[0]], pts[e[1]], col, t);
}

// ── Geometry ─────────────────────────────────────────────────────────────────

// Fixed-size limb list: avoids heap allocation per player per frame.
// Max limbs: R15 has 15, R6 has 6 — 16 slots is safe.
struct LimbList {
    std::uintptr_t addr[16];
    int count = 0;
    void push(std::uintptr_t a) { if (a && count < 16) addr[count++] = a; }
};

static LimbList limb_addrs(const Limbs& l)
{
    LimbList v;
    v.push(l.head);
    if (l.r6) {
        v.push(l.torso);
        v.push(l.l_arm); v.push(l.r_arm);
        v.push(l.l_leg); v.push(l.r_leg);
    } else {
        v.push(l.upper_torso); v.push(l.lower_torso);
        v.push(l.l_upper_arm); v.push(l.l_lower_arm); v.push(l.l_hand);
        v.push(l.r_upper_arm); v.push(l.r_lower_arm); v.push(l.r_hand);
        v.push(l.l_upper_leg); v.push(l.l_lower_leg); v.push(l.l_foot);
        v.push(l.r_upper_leg); v.push(l.r_lower_leg); v.push(l.r_foot);
    }
    return v;
}

static bool part_pose(std::uintptr_t part, Vec3& pos, float rot[9], Vec3& sz)
{
    const PartData* pd = part_data(part);
    if (!pd || !pd->prim) return false;
    pos = pd->pos;
    if (!valid(pos)) return false;
    sz = pd->sz;
    if (!std::isfinite(sz.x) || !std::isfinite(sz.y) || !std::isfinite(sz.z)) return false;
    std::memcpy(rot, pd->rot, 9 * sizeof(float));
    return true;
}

static bool part_obb_bounds(const Vec3& pos, const float rot[9], const Vec3& sz,
                            const Mat4& mv, float sw, float sh, bool use_screen,
                            Vec3& wmin, Vec3& wmax,
                            float& bmin_x, float& bmin_y, float& bmax_x, float& bmax_y)
{
    if (sz.x < 0.01f && sz.y < 0.01f && sz.z < 0.01f) return false;
    const float hx = sz.x * 0.5f, hy = sz.y * 0.5f, hz = sz.z * 0.5f;
    // Precompute rotated half-extents to avoid repeated multiply inside the loop
    // Using the "max of projections" trick: for each world axis the AABB extent
    // is |R*h|. For the screen rect we still need individual corners.
    static const float lc[8][3] = {
        { -1.f, -1.f, -1.f }, { -1.f, -1.f, 1.f },
        { -1.f,  1.f, -1.f }, { -1.f,  1.f, 1.f },
        {  1.f, -1.f, -1.f }, {  1.f, -1.f, 1.f },
        {  1.f,  1.f, -1.f }, {  1.f,  1.f, 1.f },
    };

    bool any = false;
    for (int i = 0; i < 8; ++i) {
        const float lx = hx * lc[i][0];
        const float ly = hy * lc[i][1];
        const float lz = hz * lc[i][2];
        const Vec3 wc{
            pos.x + rot[0] * lx + rot[1] * ly + rot[2] * lz,
            pos.y + rot[3] * lx + rot[4] * ly + rot[5] * lz,
            pos.z + rot[6] * lx + rot[7] * ly + rot[8] * lz,
        };
        if (wc.x < wmin.x) wmin.x = wc.x; if (wc.x > wmax.x) wmax.x = wc.x;
        if (wc.y < wmin.y) wmin.y = wc.y; if (wc.y > wmax.y) wmax.y = wc.y;
        if (wc.z < wmin.z) wmin.z = wc.z; if (wc.z > wmax.z) wmax.z = wc.z;
        if (!use_screen) continue;
        ImVec2 s{};
        if (w2s(wc, mv, sw, sh, s)) {
            if (s.x < bmin_x) bmin_x = s.x; if (s.x > bmax_x) bmax_x = s.x;
            if (s.y < bmin_y) bmin_y = s.y; if (s.y > bmax_y) bmax_y = s.y;
            any = true;
        }
    }
    return any;
}

static void draw_health_bar(ImDrawList* dl, float bx0, float by0, float by1, float frac, ImU32 fill_col)
{
    if (by1 < by0) std::swap(by0, by1);

    bx0 = std::floor(bx0 + 0.5f);
    const float bx1 = bx0 + 2.0f;
    by0 = std::floor(by0 + 0.5f);
    by1 = std::floor(by1 + 0.5f);

    const float h = by1 - by0;
    if (h <= 1.0f) return;

    frac = std::clamp(frac, 0.f, 1.f);

    dl->AddRectFilled(ImVec2{ bx0, by0 }, ImVec2{ bx1, by1 }, IM_COL32(0, 0, 0, 200));

    const float fill_top = std::max(by1 - h * frac, by0);
    if (frac > 0.001f)
        dl->AddRectFilled(ImVec2{ bx0, fill_top }, ImVec2{ bx1, by1 }, fill_col);

    const ImU32 ob = IM_COL32(0, 0, 0, 255);
    dl->AddRectFilled(ImVec2{ bx0 - 1.f, by0 - 1.f }, ImVec2{ bx1 + 1.f, by0 }, ob);
    dl->AddRectFilled(ImVec2{ bx0 - 1.f, by1 }, ImVec2{ bx1 + 1.f, by1 + 1.f }, ob);
    dl->AddRectFilled(ImVec2{ bx0 - 1.f, by0 }, ImVec2{ bx0, by1 }, ob);
    dl->AddRectFilled(ImVec2{ bx1, by0 }, ImVec2{ bx1 + 1.f, by1 }, ob);
}

// ── Main draw loop ───────────────────────────────────────────────────────────

void draw(const std::vector<rbx::Player>& list)
{
    if (!enabled) return;
    if (!rbx::visual_eng) return;
    if (!focused()) return;

    g_now = GetTickCount64();

    const Mat4 mv = rbx::view_matrix();
    const ImVec2 disp = ImGui::GetIO().DisplaySize;
    const float sw = disp.x;
    const float sh = disp.y;
    if (sw <= 0.f || sh <= 0.f) return;

    Vec3 cam{};
    if (rbx::camera && off::CameraPos)
        cam = mem::read<Vec3>(rbx::camera + off::CameraPos);

    auto* dl = ImGui::GetBackgroundDrawList();

    // Squared distance threshold — avoids sqrtf for the cull check.
    const float max_dist_sq = max_dist * max_dist;

    for (const auto& p : list) {
        if (!p.character) continue;
        if (sv::dead_check && p.humanoid && p.health <= 0.f) continue;
        if (sv::team_check && p.friendly) continue;

        const bool me = (p.addr == rbx::local_player);
        if (me && !self) continue;

        const Vec3 rp = p.hrp ? part_pos(p.hrp) : p.pos;
        if (!valid(rp)) continue;

        const Vec3 delta = rp - cam;
        const float dist_sq = delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
        if (dist_sq > max_dist_sq) continue;

        // sqrtf only when actually needed for labels/flags
        const float dist = (distance || (flags && (flag_sel & ((1 << 4) | (1 << 5))))) ?
                           sqrtf(dist_sq) : 0.f;

        const Limbs l = limbs_of(p.character);
        if (!l.head || !l.hrp) continue;

        // ── Screen-space box from head/hrp (fast fallback anchor) ──────────
        float x0{}, x1{}, y0{}, y1{};
        {
            const Vec3 hp = part_pos(l.head);
            if (!valid(hp)) continue;

            const Vec3 top3{ hp.x, hp.y + 0.5f, hp.z };
            const Vec3 bot3{ rp.x, rp.y - (l.r6 ? 3.0f : 2.5f), rp.z };

            ImVec2 top{}, bot{};
            if (!w2s(top3, mv, sw, sh, top)) continue;
            if (!w2s(bot3, mv, sw, sh, bot)) continue;

            const float h = bot.y - top.y;
            const float w = h * 0.55f;
            x0 = top.x - w * 0.5f;
            x1 = top.x + w * 0.5f;
            y0 = top.y;
            y1 = bot.y;
        }

        if (x0 < -500.f || y0 < -500.f || x1 > sw + 500.f || y1 > sh + 500.f) continue;

        const ImU32 box_col = (friendly && p.friendly) ? to_col(friendly_color) : to_col(box_color);

        // ── Distance LOD ────────────────────────────────────────────────────
        // The anchor rect above is already a decent box. Per-limb OBB geometry
        // (and mesh bounds) exists to hug the avatar closely, which only matters
        // up close — for everyone whose box would be smaller than ~45px we skip
        // it entirely. This is the big scaling win in full lobbies: far players
        // cost nothing per frame instead of ~16 part reads + transforms each.
        const float anchor_h = y1 - y0;
        const bool detailed = anchor_h >= 45.0f;

        // ── OBB geometry pass ───────────────────────────────────────────────
        float bx0 = 1e9f, by0 = 1e9f, bx1 = -1e9f, by1 = -1e9f;
        Vec3 wmin{ 1e9f, 1e9f, 1e9f }, wmax{ -1e9f, -1e9f, -1e9f };
        bool have_geo = false;

        if (detailed) {
            const LimbList limbs = limb_addrs(l);
            for (int li = 0; li < limbs.count; ++li) {
                Vec3 pos{}, sz{};
                float rot[9]{};
                if (!part_pose(limbs.addr[li], pos, rot, sz)) continue;
                part_obb_bounds(pos, rot, sz, mv, sw, sh, limbs.addr[li] != p.hrp,
                                wmin, wmax, bx0, by0, bx1, by1);
            }
            have_geo = wmin.x <= wmax.x;

            if (bounding_type == 1 && p.character) {
                float mx0 = 1e9f, my0 = 1e9f, mx1 = -1e9f, my1 = -1e9f;
                Vec3 mwmin{ 1e9f, 1e9f, 1e9f }, mwmax{ -1e9f, -1e9f, -1e9f };
                if (meshes::expand_bounds(p.character, mv, sw, sh, mx0, mx1, my0, my1, mwmin, mwmax)) {
                    bx0 = mx0; by0 = my0; bx1 = mx1; by1 = my1;
                    wmin = mwmin; wmax = mwmax;
                    have_geo = true;
                }
            }

            if (bounding_type == 0 && have_geo) {
                // Use cached part data for head size — no extra RPM.
                const Vec3 hp = part_pos(l.head);
                if (valid(hp)) {
                    float head_half = 0.5f;
                    const PartData* hpd = part_data(l.head);
                    if (hpd && hpd->prim)
                        head_half = (std::max)(hpd->sz.y * 0.5f, 0.01f);

                    const Vec3 top3{ hp.x, hp.y + head_half, hp.z };
                    ImVec2 ts{};
                    if (w2s(top3, mv, sw, sh, ts) && ts.y < by0 - 2.f)
                        by0 = ts.y;

                    float feet_y = rp.y - (l.r6 ? 3.0f : 2.5f);
                    auto feet_lo = [&](std::uintptr_t f) {
                        const PartData* fpd = part_data(f);
                        if (!fpd || !fpd->prim) return;
                        float fy = fpd->pos.y - fpd->sz.y * 0.5f;
                        if (fy < feet_y) feet_y = fy;
                    };
                    feet_lo(l.l_foot);
                    feet_lo(l.r_foot);

                    const Vec3 bot3{ hp.x, feet_y, hp.z };
                    ImVec2 bs{};
                    if (w2s(bot3, mv, sw, sh, bs) && bs.y > by1 + 2.f)
                        by1 = bs.y;
                }
            }

            if (have_geo && bx0 < bx1 && by0 < by1) {
                x0 = bx0; y0 = by0; x1 = bx1; y1 = by1;
            }
        }

        // ── ESP elements ────────────────────────────────────────────────────
        if (box) {
            draw_box_fill(dl, x0, y0, x1, y1);
            if (detailed && skeleton) render_skeleton(dl, l, mv, sw, sh);

            if (box_mode == 2 && have_geo) {
                ImVec2 pts[8];
                bool all_ok = true;
                for (int i = 0; i < 8 && all_ok; ++i) {
                    const Vec3 c{
                        (i & 4) ? wmax.x : wmin.x,
                        (i & 2) ? wmax.y : wmin.y,
                        (i & 1) ? wmax.z : wmin.z
                    };
                    if (!w2s(c, mv, sw, sh, pts[i])) all_ok = false;
                }
                if (all_ok)
                    draw_box_3d_edges(dl, pts, box_col);
                else
                    draw_box_px(dl, x0, y0, x1, y1, box_col);
            }
            else if (box_mode == 1)
                draw_corner_box_px(dl, x0, y0, x1, y1, box_col);
            else
                draw_box_px(dl, x0, y0, x1, y1, box_col);
        }
        else if (detailed && skeleton) {
            render_skeleton(dl, l, mv, sw, sh);
        }

        if (health_bar && p.max_health > 0.01f) {
            float f = p.health / p.max_health;
            if (!std::isfinite(f)) f = 1.f;
            f = std::clamp(f, 0.f, 1.f);
            const ImU32 hcol = IM_COL32(static_cast<int>(255.f * (1.f - f)),
                                         static_cast<int>(255.f * f), 0, 255);
            draw_health_bar(dl, std::floor(x0 - 4.f), y0, y1, f, hcol);
        }

        if (name_tag && !p.name.empty()) {
            const ImVec2 ts = text_size(p.name);
            outlined_text(dl, ImVec2{ (x0 + x1) * 0.5f - ts.x * 0.5f, y0 - ts.y - 2.f },
                          p.name, to_col(name_color));
        }

        float below = y1 + 2.f;
        if (distance) {
            char buf[32];
            const int di = static_cast<int>(dist);
            const int len = std::snprintf(buf, sizeof(buf), "%dm", di);
            const ImVec2 ts = text_size(buf);
            outlined_text(dl, ImVec2{ (x0 + x1) * 0.5f - ts.x * 0.5f, below }, buf, to_col(distance_color));
            below += ts.y + 3.f;
        }

        if (tool && !l.tool.empty()) {
            const ImVec2 ts = text_size(l.tool);
            outlined_text(dl, ImVec2{ (x0 + x1) * 0.5f - ts.x * 0.5f, below }, l.tool, to_col(tool_color));
            below += ts.y + 3.f;
        }

        if (flags) {
            // Re-use cached prim for velocity — no extra RPM.
            Vec3 vel{};
            const PartData* hrppd = part_data(p.hrp);
            if (hrppd && hrppd->prim && off::Velocity)
                vel = mem::read<Vec3>(hrppd->prim + off::Velocity);
            const float speed_h = sqrtf(vel.x * vel.x + vel.z * vel.z);

            float fx = x1 + 4.f;
            float fy = y0;
            auto flag_text = [&](const char* t, ImU32 c) {
                outlined_text(dl, ImVec2{ fx, fy }, t, c);
                fy += font_size + 2.f;
            };

            const ImU32 fc = to_col(flags_color);
            char buf[32];
            if (flag_sel & (1 << 0)) {
                if (vel.y > 2.f)         flag_text("Jumping", fc);
                else if (vel.y < -2.f)   flag_text("Falling", fc);
                else if (speed_h > 1.5f) flag_text("Running", fc);
                else                     flag_text("Idle",    fc);
            }
            if (flag_sel & (1 << 1)) flag_text(l.r6 ? "R6" : "R15", fc);
            if ((flag_sel & (1 << 2)) && p.max_health > 0.f) {
                std::snprintf(buf, sizeof(buf), "%d%% HP",
                    static_cast<int>(p.health / p.max_health * 100.f));
                flag_text(buf, fc);
            }
            if ((flag_sel & (1 << 3)) && !l.tool.empty()) flag_text(l.tool.c_str(), fc);
            if (flag_sel & (1 << 4)) {
                const float d = dist > 0.f ? dist : sqrtf(dist_sq);
                std::snprintf(buf, sizeof(buf), "%dm", static_cast<int>(d));
                flag_text(buf, fc);
            }
            if (flag_sel & (1 << 5)) {
                std::snprintf(buf, sizeof(buf), "%.1f u/s", speed_h);
                flag_text(buf, fc);
            }
        }

        if (head_dot) {
            const Vec3 hp = part_pos(l.head);
            ImVec2 hs{};
            if (valid(hp) && w2s(hp, mv, sw, sh, hs)) {
                const float d = dist > 0.f ? dist : sqrtf(dist_sq);
                const float r = head_dot_size * std::clamp(200.f / (std::max)(d, 10.f), 0.6f, 1.4f);
                dl->AddCircleFilled(hs, r, to_col(head_dot_color), 16);
                dl->AddCircle(hs, r + 1.f, IM_COL32(0, 0, 0, 180), 16, 0.5f);
            }
        }

        if (view_direction && p.hrp) {
            // Rotation already in PartData cache — zero extra RPM.
            const PartData* pd = part_data(p.hrp);
            if (pd && pd->prim) {
                const float* rot = pd->rot;
                Vec3 look{ -rot[2], -rot[5], -rot[8] };
                const float len_sq = look.x * look.x + look.y * look.y + look.z * look.z;
                if (len_sq > 1e-6f) {
                    const float inv = 1.f / sqrtf(len_sq);
                    look.x *= inv; look.y *= inv; look.z *= inv;

                    const float len = std::clamp(view_dir_length, 1.f, 50.f);
                    const Vec3 end{ rp.x + look.x * len, rp.y + look.y * len, rp.z + look.z * len };
                    ImVec2 s0{}, s1{};
                    if (w2s(rp, mv, sw, sh, s0) && w2s(end, mv, sw, sh, s1)) {
                        const ImU32 c = to_col(view_dir_color);
                        line(dl, s0, s1, c, 2.f, true);
                        dl->AddCircleFilled(s1, 2.5f, c, 10);
                    }
                }
            }
        }
    }
}

}
