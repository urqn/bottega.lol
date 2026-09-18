#include "esp.h"
#include "settings.h"
#include "mem.h"
#include "offsets.h"
#include "overlay.h"
#include "imgui.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <string>
#include <unordered_map>


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

struct Limbs {
    std::uintptr_t head{}, hrp{}, torso{}, upper_torso{}, lower_torso{};
    std::uintptr_t l_arm{}, r_arm{}, l_leg{}, r_leg{};
    std::uintptr_t l_upper_arm{}, l_lower_arm{}, l_hand{}, r_upper_arm{}, r_lower_arm{}, r_hand{};
    std::uintptr_t l_upper_leg{}, l_lower_leg{}, l_foot{}, r_upper_leg{}, r_lower_leg{}, r_foot{};
    std::string tool{};
    bool r6{};
    int  stamp{ -1 };
};

static std::unordered_map<std::uintptr_t, Limbs> limb_cache;
static std::unordered_map<std::uintptr_t, std::pair<int, Vec3>> pos_cache;
static int frame_stamp = 0;
static constexpr int k_pos_fresh = 2; // ~30Hz at a 60fps render loop

void invalidate()
{
    limb_cache.clear();
    pos_cache.clear();
    frame_stamp = 0;
}

// part positions are cached for a couple of frames so the render loop does not
// re-read the whole limb set of every player every frame. at ~30Hz the boxes
// stay glued to the model but the memory read storm is gone.
static Vec3 part_pos(std::uintptr_t part)
{
    if (!part || !off::Primitive || !off::Position) return {};

    auto it = pos_cache.find(part);
    if (it != pos_cache.end() && frame_stamp - it->second.first < k_pos_fresh)
        return it->second.second;

    std::uintptr_t prim = mem::read<std::uintptr_t>(part + off::Primitive);
    Vec3 p{};
    if (prim) p = mem::read<Vec3>(prim + off::Position);
    pos_cache[part] = { frame_stamp, p };
    return p;
}

static Limbs limbs_of(std::uintptr_t character)
{
    if (!character)
        return Limbs{};

    Limbs& l = limb_cache[character];

    const bool stale = (l.stamp < 0) || (frame_stamp - l.stamp > 60) ||
                       (!l.head || !l.hrp) || (!valid(part_pos(l.head)) && !valid(part_pos(l.hrp)));

    if (!stale)
        return l;

    l = Limbs{};
    l.stamp = frame_stamp;

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


static void outlined_text(ImDrawList* dl, const ImVec2& pos, const std::string& text, ImU32 col)
{
    if (text.empty()) return;
    ImFont* f = font();
    dl->AddText(f, font_size, ImVec2{ pos.x - 1.f, pos.y }, IM_COL32(0, 0, 0, 255), text.c_str());
    dl->AddText(f, font_size, ImVec2{ pos.x + 1.f, pos.y }, IM_COL32(0, 0, 0, 255), text.c_str());
    dl->AddText(f, font_size, ImVec2{ pos.x, pos.y - 1.f }, IM_COL32(0, 0, 0, 255), text.c_str());
    dl->AddText(f, font_size, ImVec2{ pos.x, pos.y + 1.f }, IM_COL32(0, 0, 0, 255), text.c_str());
    dl->AddText(f, font_size, pos, col, text.c_str());
}

static ImVec2 text_size(const std::string& text)
{
    if (text.empty()) return ImVec2{ 0.f, font_size };
    return font()->CalcTextSizeA(font_size, FLT_MAX, 0.f, text.c_str());
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
        for (int i = 0; i < 3; ++i) {
            if (!c[i]) return;
            if (i == 0) continue;
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

static bool dynamic_bounds(const Limbs& l, const Mat4& mv, float sw, float sh,
                           float& x0, float& x1, float& y0, float& y1)
{
    if (!l.head || !l.hrp) return false;

    const Vec3 hp = part_pos(l.head);
    const Vec3 rp = part_pos(l.hrp);
    if (!valid(hp) || !valid(rp)) return false;

    const bool r6 = l.r6;
    Vec3 pts[20];
    int n = 0;
    pts[n++] = Vec3{ hp.x, hp.y + 0.6f, hp.z };
    pts[n++] = hp;

    const std::uintptr_t extra[] = {
        r6 ? l.torso : l.upper_torso, r6 ? l.l_arm : l.l_upper_arm, r6 ? l.r_arm : l.r_upper_arm,
        r6 ? l.l_leg : l.l_upper_leg, r6 ? l.r_leg : l.r_upper_leg, r6 ? 0 : l.lower_torso,
        r6 ? 0 : l.l_lower_arm, r6 ? 0 : l.l_hand, r6 ? 0 : l.r_lower_arm, r6 ? 0 : l.r_hand,
        r6 ? 0 : l.l_lower_leg, r6 ? 0 : l.l_foot, r6 ? 0 : l.r_lower_leg, r6 ? 0 : l.r_foot
    };

    for (auto addr : extra) {
        if (!addr || n >= 17) continue;
        const Vec3 p = part_pos(addr);
        if (!valid(p)) continue;
        pts[n++] = p;
    }

    pts[n++] = rp;
    pts[n++] = Vec3{ rp.x, rp.y - (r6 ? 3.0f : 2.5f), rp.z };

    bool any = false;
    float mnx = 1e9f, mxx = -1e9f, mny = 1e9f, mxy = -1e9f;
    for (int i = 0; i < n; ++i) {
        ImVec2 s{};
        if (!w2s(pts[i], mv, sw, sh, s)) continue;
        any = true;
        mnx = (std::min)(mnx, s.x);
        mxx = (std::max)(mxx, s.x);
        mny = (std::min)(mny, s.y);
        mxy = (std::max)(mxy, s.y);
    }
    if (!any) return false;

    const float pad = std::clamp((mxy - mny) * 0.06f, 3.0f, 10.0f);
    x0 = mnx - pad * 0.8f;
    x1 = mxx + pad * 0.8f;
    y0 = mny - pad;
    y1 = mxy + pad;
    return x1 > x0 && y1 > y0;
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

static void draw_corner_box(ImDrawList* dl, float x0, float y0, float x1, float y1, ImU32 col)
{
    const float w = x1 - x0;
    const float h = y1 - y0;

    float len = (w < h ? w : h) * 0.25f;
    if (len < 3.0f) len = 3.0f;
    if (len > w * 0.5f) len = w * 0.5f;
    if (len > h * 0.5f) len = h * 0.5f;

    for (int pass = 0; pass < 2; ++pass)
    {
        const ImU32 c = pass == 0 ? IM_COL32(0, 0, 0, 255) : col;
        const float t = pass == 0 ? 3.0f : 1.0f;

        dl->AddLine(ImVec2{ x0, y0 }, ImVec2{ x0 + len, y0 }, c, t);
        dl->AddLine(ImVec2{ x0, y0 }, ImVec2{ x0, y0 + len }, c, t);

        dl->AddLine(ImVec2{ x1, y0 }, ImVec2{ x1 - len, y0 }, c, t);
        dl->AddLine(ImVec2{ x1, y0 }, ImVec2{ x1, y0 + len }, c, t);

        dl->AddLine(ImVec2{ x0, y1 }, ImVec2{ x0 + len, y1 }, c, t);
        dl->AddLine(ImVec2{ x0, y1 }, ImVec2{ x0, y1 - len }, c, t);

        dl->AddLine(ImVec2{ x1, y1 }, ImVec2{ x1 - len, y1 }, c, t);
        dl->AddLine(ImVec2{ x1, y1 }, ImVec2{ x1, y1 - len }, c, t);
    }
}

static void draw_box(ImDrawList* dl, float x0, float y0, float x1, float y1, ImU32 col)
{
    dl->AddRect(ImVec2{ x0 - 1.f, y0 - 1.f }, ImVec2{ x1 + 1.f, y1 + 1.f }, IM_COL32(0, 0, 0, 255), 0.f, 0, 1.f);
    dl->AddRect(ImVec2{ x0, y0 }, ImVec2{ x1, y1 }, col, 0.f, 0, 1.f);
    dl->AddRect(ImVec2{ x0 + 1.f, y0 + 1.f }, ImVec2{ x1 - 1.f, y1 - 1.f }, IM_COL32(0, 0, 0, 255), 0.f, 0, 1.f);
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


void draw(const std::vector<rbx::Player>& list) {
    if (!enabled) return;
    if (!rbx::visual_eng) return;
    if (!focused()) return;

    ++frame_stamp;

    const Mat4 mv = rbx::view_matrix();
    const ImVec2 disp = ImGui::GetIO().DisplaySize;
    const float sw = disp.x;
    const float sh = disp.y;
    if (sw <= 0.f || sh <= 0.f) return;

    Vec3 cam{};
    if (rbx::camera && off::CameraPos)
        cam = mem::read<Vec3>(rbx::camera + off::CameraPos);

    auto* dl = ImGui::GetBackgroundDrawList();

    for (const auto& p : list) {
        if (!p.character) continue;
        if (sv::dead_check && p.humanoid && p.health <= 0.f) continue;
        if (sv::team_check && p.friendly) continue;

        const bool me = (p.addr == rbx::local_player);
        if (me && !self) continue;

        const Vec3 rp = p.hrp ? part_pos(p.hrp) : p.pos;
        if (!valid(rp)) continue;

        const Vec3 delta = rp - cam;
        const float dist = sqrtf(delta.dot(delta));
        if (dist > max_dist) continue;

        const Limbs l = limbs_of(p.character);
        if (!l.head || !l.hrp) continue;

        float x0{}, x1{}, y0{}, y1{};
        if (box_style == 1) {
            if (!dynamic_bounds(l, mv, sw, sh, x0, x1, y0, y1)) continue;
        }
        else {
            const Vec3 hp = part_pos(l.head);
            if (!valid(hp)) continue;

            const bool r6 = l.r6;
            const Vec3 top3{ hp.x, hp.y + 0.5f, hp.z };
            const Vec3 bot3{ rp.x, rp.y - (r6 ? 3.0f : 2.5f), rp.z };

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

        if (box) {
            draw_box_fill(dl, x0, y0, x1, y1);
            if (skeleton) render_skeleton(dl, l, mv, sw, sh);
            if (box_type == 1) draw_corner_box(dl, x0, y0, x1, y1, box_col);
            else draw_box(dl, x0, y0, x1, y1, box_col);
        }
        else if (skeleton) {
            render_skeleton(dl, l, mv, sw, sh);
        }

        if (health_bar && p.max_health > 0.01f) {
            float f = p.health / p.max_health;
            if (!std::isfinite(f)) f = 1.f;
            f = std::clamp(f, 0.f, 1.f);
            const ImU32 hcol = IM_COL32(static_cast<int>(255.f * (1.f - f)), static_cast<int>(255.f * f), 0, 255);
            draw_health_bar(dl, std::floor(x0 - 4.f), y0, y1, f, hcol);
        }

        if (name_tag && !p.name.empty()) {
            const ImVec2 ts = text_size(p.name);
            outlined_text(dl, ImVec2{ (x0 + x1) * 0.5f - ts.x * 0.5f, y0 - ts.y - 2.f }, p.name, to_col(name_color));
        }

        float below = y1 + 2.f;
        if (distance) {
            const std::string dt = std::to_string(static_cast<int>(dist)) + "m";
            const ImVec2 ts = text_size(dt);
            outlined_text(dl, ImVec2{ (x0 + x1) * 0.5f - ts.x * 0.5f, below }, dt, to_col(distance_color));
            below += ts.y + 3.f;
        }

        if (tool && !l.tool.empty()) {
            const ImVec2 ts = text_size(l.tool);
            outlined_text(dl, ImVec2{ (x0 + x1) * 0.5f - ts.x * 0.5f, below }, l.tool, to_col(tool_color));
            below += ts.y + 3.f;
        }

        if (flags) {
            const std::uintptr_t prim = p.hrp ? mem::read<std::uintptr_t>(p.hrp + off::Primitive) : 0;
            const Vec3 vel = (prim && off::Velocity) ? mem::read<Vec3>(prim + off::Velocity) : Vec3{};
            const float speed_h = sqrtf(vel.x * vel.x + vel.z * vel.z);

            float fx = x1 + 4.f;
            float fy = y0;
            auto flag_text = [&](const std::string& t, ImU32 c) {
                outlined_text(dl, ImVec2{ fx, fy }, t, c);
                fy += font_size + 2.f;
            };

            const ImU32 fc = to_col(flags_color);
            if (flag_sel & (1 << 0)) {
                if (vel.y > 2.f)        flag_text("Jumping", fc);
                else if (vel.y < -2.f)  flag_text("Falling", fc);
                else if (speed_h > 1.5f) flag_text("Running", fc);
                else                     flag_text("Idle", fc);
            }
            if (flag_sel & (1 << 1)) flag_text(l.r6 ? "R6" : "R15", fc);
            if ((flag_sel & (1 << 2)) && p.max_health > 0.f)
                flag_text(std::to_string(static_cast<int>(p.health / p.max_health * 100.f)) + "% HP", fc);
            if ((flag_sel & (1 << 3)) && !l.tool.empty()) flag_text(l.tool, fc);
            if (flag_sel & (1 << 4)) flag_text(std::to_string(static_cast<int>(dist)) + "m", fc);
            if (flag_sel & (1 << 5)) {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%.1f u/s", speed_h);
                flag_text(buf, fc);
            }
        }

        if (head_dot) {
            const Vec3 hp = part_pos(l.head);
            ImVec2 hs{};
            if (valid(hp) && w2s(hp, mv, sw, sh, hs)) {
                const float r = head_dot_size * std::clamp(200.f / (std::max)(dist, 10.f), 0.6f, 1.4f);
                dl->AddCircleFilled(hs, r, to_col(head_dot_color), 16);
                dl->AddCircle(hs, r + 1.f, IM_COL32(0, 0, 0, 180), 16, 0.5f);
            }
        }

        if (view_direction && p.hrp && off::Primitive) {
            const std::uintptr_t prim = mem::read<std::uintptr_t>(p.hrp + off::Primitive);
            if (prim && off::Rotation) {
                float rot[9]{};
                for (int i = 0; i < 9; ++i)
                    rot[i] = mem::read<float>(prim + off::Rotation + static_cast<std::uint64_t>(i) * sizeof(float));
                Vec3 look{ -rot[2], -rot[5], -rot[8] };
                const float len_sq = look.x * look.x + look.y * look.y + look.z * look.z;
                if (len_sq > 1e-6f) {
                    const float inv = 1.f / sqrtf(len_sq);
                    look.x *= inv; look.y *= inv; look.z *= inv;

                    const Vec3 hp = part_pos(l.head);
                    if (valid(hp) && valid(rp)) {
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

}
