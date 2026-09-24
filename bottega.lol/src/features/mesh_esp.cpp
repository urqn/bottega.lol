#include "mesh_esp.h"
#include "mem.h"
#include "rbx.h"
#include "offsets.h"
#include "mesh_offsets.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <unordered_map>
#include <unordered_set>

namespace {
// ---- content string readers (bottega layout: lenstr first, C-string fallback)

std::string read_content_string(std::uintptr_t addr)
{
    if (!mem::is_valid(addr)) return {};
    std::string s = mem::read_lenstr(addr);
    if (s.empty() || s == "Unknown") s = mem::read_string(addr);
    if (s.empty() || s == "Unknown")
    {
        const std::uintptr_t p = mem::read<std::uintptr_t>(addr);
        if (mem::is_valid(p))
        {
            s = mem::read_lenstr(p);
            if (s.empty() || s == "Unknown") s = mem::read_string(p);
        }
    }
    if (s == "Unknown") return {};
    return s;
}
} // namespace

std::string CleanAssetId(const std::string& raw)
{
    if (raw.empty() || raw == "Unknown") return {};
    if (raw.rfind("rbxassetid://", 0) == 0) return raw.substr(13);
    const auto q = raw.find("?id=");
    if (q != std::string::npos && q + 4 < raw.size())
    {
        std::string id = raw.substr(q + 4);
        const auto end = id.find_first_of("& \n\r\t");
        if (end != std::string::npos) id.resize(end);
        return id;
    }
    const auto pos = raw.find_last_of("=/");
    if (pos != std::string::npos && pos + 1 < raw.size())
        return raw.substr(pos + 1);
    return raw;
}

namespace {

std::uintptr_t FindMeshContentProvider()
{
    if (!mem::is_valid(rbx::datamodel)) return 0;
    for (const auto& c : rbx::children(rbx::datamodel))
    {
        if (c && rbx::classname(c) == "MeshContentProvider")
            return c;
    }
    return 0;
}

bool LooksLikeMeshData(std::uintptr_t md, int& vtx_count, int& fac_count,
                       std::uintptr_t& vtx_start, std::uintptr_t& fac_start)
{
    if (!mem::is_valid(md)) return false;
    vtx_start = mem::read<std::uintptr_t>(md + moff::MeshData_VertexStart);
    const std::uintptr_t vtx_end = mem::read<std::uintptr_t>(md + moff::MeshData_VertexEnd);
    fac_start = mem::read<std::uintptr_t>(md + moff::MeshData_FaceStart);
    const std::uintptr_t fac_end = mem::read<std::uintptr_t>(md + moff::MeshData_FaceEnd);

    vtx_count = (vtx_end > vtx_start) ? (int)((vtx_end - vtx_start) / sizeof(MeshVertex)) : 0;
    fac_count = (fac_end > fac_start) ? (int)((fac_end - fac_start) / sizeof(MeshFace)) : 0;

    return vtx_count > 0 && vtx_count < 50000 &&
           fac_count > 0 && fac_count < 100000 &&
           mem::is_valid(vtx_start) && mem::is_valid(fac_start);
}

void CollectKeys(const std::string& raw, const std::string& cleaned, std::vector<std::string>& keys)
{
    auto add = [&](const std::string& k) {
        if (k.empty()) return;
        for (const auto& e : keys)
            if (e == k) return;
        keys.push_back(k);
    };
    add(cleaned);
    add(raw);
    if (raw.rfind("rbxasset://", 0) == 0) add(raw.substr(11));
    if (raw.rfind("rbxassetid://", 0) == 0) add(raw.substr(13));
}

} // namespace

MeshCache& MeshCache::Get()
{
    static MeshCache inst;
    return inst;
}

void MeshCache::Refresh(bool force)
{
    const ULONGLONG now = GetTickCount64();
    if (last_refresh_)
    {
        const ULONGLONG gap = force ? 250ull : 1000ull;
        if ((now - last_refresh_) < gap) return;
    }

    const std::uintptr_t mcp = FindMeshContentProvider();
    if (!mem::is_valid(mcp)) return;

    // phantomX note: theo sometimes reports 0xf0 (module); live usually 0xd8
    static const std::uintptr_t k_cache_offs[] = {
        moff::MeshContentProvider_Cache, 0xd8, 0xf0, 0xc8, 0xe0, 0xe8
    };

    std::uintptr_t sentinel = 0;
    std::uintptr_t node = 0;
    for (std::uintptr_t off : k_cache_offs)
    {
        const std::uintptr_t c = mem::read<std::uintptr_t>(mcp + off);
        if (!mem::is_valid(c)) continue;
        const std::uintptr_t l = mem::read<std::uintptr_t>(c + moff::MeshContentProvider_LRUCache);
        if (!mem::is_valid(l)) continue;
        const std::uintptr_t s = mem::read<std::uintptr_t>(l + 0x08);
        if (!mem::is_valid(s)) continue;
        const std::uintptr_t n = mem::read<std::uintptr_t>(s);
        if (!mem::is_valid(n) || n == s) continue;
        sentinel = s;
        node = n;
        break;
    }
    if (!mem::is_valid(sentinel) || !mem::is_valid(node))
        return;

    last_refresh_ = now;

    std::unordered_map<std::string, std::shared_ptr<CachedMesh>> temp;
    int max_nodes = 4000;

    while (mem::is_valid(node) && node != sentinel && max_nodes-- > 0)
    {
        const std::string raw_id = read_content_string(node + moff::MeshContentProvider_AssetID);
        const std::string id = CleanAssetId(raw_id);

        if (!id.empty() && temp.find(id) == temp.end())
        {
            const std::uintptr_t pointer = mem::read<std::uintptr_t>(
                node + moff::MeshContentProvider_ToMeshData);

            std::uintptr_t mesh_data = 0;
            int vtx_count = 0, fac_count = 0;
            std::uintptr_t vtx_start = 0, fac_start = 0;

            if (mem::is_valid(pointer))
            {
                mesh_data = mem::read<std::uintptr_t>(pointer + moff::MeshContentProvider_MeshData);
                if (!LooksLikeMeshData(mesh_data, vtx_count, fac_count, vtx_start, fac_start))
                {
                    // sometimes ToMeshData already points at MeshData
                    if (LooksLikeMeshData(pointer, vtx_count, fac_count, vtx_start, fac_start))
                        mesh_data = pointer;
                    else
                        mesh_data = 0;
                }
            }

            if (mesh_data && vtx_count > 0)
            {
                auto mesh = std::make_shared<CachedMesh>();
                mesh->asset_id = id;
                mesh->vertices.resize((std::size_t)vtx_count);
                mesh->faces.resize((std::size_t)fac_count);

                const std::size_t vb = (std::size_t)vtx_count * sizeof(MeshVertex);
                const std::size_t fb = (std::size_t)fac_count * sizeof(MeshFace);
                if (mem::read_block(vtx_start, mesh->vertices.data(), vb) &&
                    mem::read_block(fac_start, mesh->faces.data(), fb) &&
                    std::isfinite(mesh->vertices[0].pos[0]))
                {
                    std::vector<std::string> keys;
                    CollectKeys(raw_id, id, keys);
                    for (const auto& k : keys)
                        temp.emplace(k, mesh);
                }
            }
        }

        node = mem::read<std::uintptr_t>(node);
    }

    std::lock_guard<std::mutex> lk(mtx_);
    for (auto& kv : temp)
        cache_[kv.first] = std::move(kv.second);
}

std::shared_ptr<const CachedMesh> MeshCache::FindShared(const std::string& asset_id) const
{
    if (asset_id.empty() || asset_id == "Unknown")
        return nullptr;

    std::vector<std::string> keys;
    const std::string cleaned = CleanAssetId(asset_id);
    CollectKeys(asset_id, cleaned, keys);

    std::lock_guard<std::mutex> lk(mtx_);
    for (const auto& k : keys)
    {
        auto it = cache_.find(k);
        if (it == cache_.end() || !it->second)
            continue;
        return it->second;
    }
    return nullptr;
}

std::size_t MeshCache::Count() const
{
    std::lock_guard<std::mutex> lk(mtx_);
    return cache_.size();
}

// ---- mesh parser --------------------------------------------------------

namespace mesh_parser {
namespace {
using std::uintptr_t;

bool IsBasePartClass(const std::string& cls)
{
    return cls == "Part" || cls == "MeshPart" || cls == "UnionOperation" ||
           cls == "NegateOperation" || cls == "IntersectOperation" ||
           cls == "TrussPart" || cls == "WedgePart" || cls == "CornerWedgePart" ||
           cls == "Seat" || cls == "VehicleSeat" || cls == "SpawnLocation";
}

bool IsSkipClass(const std::string& cls)
{
    return cls == "Humanoid" || cls == "Script" || cls == "LocalScript" ||
           cls == "ModuleScript" || cls == "Sound" || cls == "Animation" ||
           cls == "Animator" || cls == "BindableEvent" || cls == "BindableFunction" ||
           cls == "RemoteEvent" || cls == "RemoteFunction" || cls == "Attachment" ||
           cls == "Motor6D" || cls == "Weld" || cls == "WeldConstraint" ||
           cls == "ManualWeld" || cls == "Snap" || cls == "BodyColors" ||
           cls == "Shirt" || cls == "Pants" || cls == "ShirtGraphic" ||
           cls == "BodyGyro" || cls == "BodyVelocity" || cls == "BodyForce" ||
           cls == "Highlight" || cls == "BillboardGui" || cls == "SurfaceGui" ||
           cls == "ProximityPrompt" || cls == "ClickDetector" ||
           cls == "WrapTarget" || cls == "WrapLayer" || cls == "SurfaceAppearance" ||
           cls == "NoCollisionConstraint";
}

bool NameHas(const std::string& s, const char* needle)
{
    if (s.empty() || !needle) return false;
    std::string a = s;
    std::string b = needle;
    for (char& c : a) c = (char)std::tolower((unsigned char)c);
    for (char& c : b) c = (char)std::tolower((unsigned char)c);
    return a.find(b) != std::string::npos;
}

bool IsSkipPartName(const std::string& name)
{
    if (name.empty()) return false;
    if (name == "HumanoidRootPart" || name == "CollisionCapsule") return true;
    return NameHas(name, "collision") || NameHas(name, "hitbox") ||
           NameHas(name, "capsule") || NameHas(name, "nocol");
}

bool IsSkipContainerName(const std::string& name)
{
    return NameHas(name, "weapon") || NameHas(name, "gun") ||
           NameHas(name, "viewmodel") || NameHas(name, "firstperson") ||
           NameHas(name, "viewarms") || NameHas(name, "fakearm");
}

bool IsAccessoryClass(const std::string& cls)
{
    return cls == "Accessory" || cls == "Hat" || cls == "Accoutrement";
}

Kind Classify(const std::string& part_name, const std::string& container, bool under_acc)
{
    if (under_acc)
    {
        if (NameHas(part_name, "face") || NameHas(container, "face"))
            return Kind::Face;
        if (NameHas(container, "hair") || NameHas(part_name, "hair") ||
            NameHas(container, "ponytail") || NameHas(container, "bun") ||
            NameHas(container, "beetle") || NameHas(container, "ringo"))
            return Kind::Hair;
        return Kind::Accessory;
    }

    static const char* k_body[] = {
        "Head", "Torso", "UpperTorso", "LowerTorso",
        "LeftUpperArm", "LeftLowerArm", "LeftHand", "Left Arm",
        "RightUpperArm", "RightLowerArm", "RightHand", "Right Arm",
        "LeftUpperLeg", "LeftLowerLeg", "LeftFoot", "Left Leg",
        "RightUpperLeg", "RightLowerLeg", "RightFoot", "Right Leg",
        "HumanoidRootPart"
    };
    for (const char* n : k_body)
    {
        if (part_name == n)
            return Kind::Body;
    }
    return Kind::Other;
}

std::string ReadSpecialMeshId(std::uintptr_t sm)
{
    return read_content_string(sm + moff::SpecialMesh_MeshId);
}

std::string ReadCharacterMeshId(std::uintptr_t cm)
{
    return read_content_string(cm + moff::CharacterMesh_MeshId);
}

std::string ReadMeshPartId(std::uintptr_t part)
{
    const std::string s = read_content_string(part + moff::MeshPart_MeshId);
    return s == "Unknown" ? std::string{} : s;
}

void PushPart(uintptr_t character, uintptr_t part, const std::string& container,
              bool under_acc, std::unordered_set<uintptr_t>& seen,
              std::vector<Entry>& out)
{
    if (!mem::is_valid(part) || seen.count(part)) return;
    seen.insert(part);

    Entry e{};
    e.part = part;
    e.character = character;
    e.name = rbx::name_of(part);
    e.class_name = rbx::classname(part);
    e.container = container;
    e.kind = Classify(e.name, container, under_acc);

    if (e.class_name == "MeshPart")
        e.mesh_id = ReadMeshPartId(part);

    for (const auto& c : rbx::children(part))
    {
        const std::string cc = rbx::classname(c);
        if (cc == "SpecialMesh" || cc == "FileMesh" || cc == "CylinderMesh" || cc == "BlockMesh")
        {
            e.special_mesh = c;
            if (e.mesh_id.empty())
                e.mesh_id = ReadSpecialMeshId(c);
            if (e.kind == Kind::Other)
                e.kind = Kind::Special;
            break;
        }
    }

    out.push_back(std::move(e));
}

void Walk(uintptr_t character, uintptr_t node, int depth,
          const std::string& container, bool under_acc,
          std::unordered_set<uintptr_t>& seen, std::vector<Entry>& out)
{
    if (depth > 12 || !mem::is_valid(node))
        return;

    const std::string cls = rbx::classname(node);
    if (IsSkipClass(cls))
        return;
    if (cls == "Tool")
        return;

    if (IsAccessoryClass(cls))
    {
        const std::string acc = rbx::name_of(node);
        for (const auto& c : rbx::children(node))
            Walk(character, c, depth + 1, acc, true, seen, out);
        return;
    }

    // accessories without an Accessory class (rare) but Handle under character
    if (!under_acc && (cls == "Model" || cls == "Folder"))
    {
        const std::string nm = rbx::name_of(node);
        if (IsSkipContainerName(nm)) return;
        const bool maybe_acc = NameHas(nm, "accessory") || NameHas(nm, "hat") ||
            NameHas(nm, "hair") || NameHas(nm, "layer") || NameHas(nm, "mesh") ||
            NameHas(nm, "armor") || NameHas(nm, "clothing") || NameHas(nm, "gear");
        for (const auto& c : rbx::children(node))
            Walk(character, c, depth + 1, maybe_acc ? nm : container, under_acc || maybe_acc, seen, out);
        return;
    }

    if (cls == "CharacterMesh")
    {
        Entry e{};
        e.character = character;
        e.name = rbx::name_of(node);
        e.class_name = cls;
        e.container = container;
        e.kind = Kind::CharacterMesh;
        e.mesh_id = ReadCharacterMeshId(node);
        out.push_back(std::move(e));
        return;
    }

    if (IsBasePartClass(cls))
    {
        const std::string nm = rbx::name_of(node);
        if (IsSkipPartName(nm)) return;

        // Handle anywhere under the character = accessory (even if parent is not Accessory)
        const bool acc = under_acc || (nm == "Handle");
        PushPart(character, node, container, acc, seen, out);
        for (const auto& c : rbx::children(node))
        {
            const std::string cc = rbx::classname(c);
            if (IsBasePartClass(cc) || IsAccessoryClass(cc) ||
                cc == "Model" || cc == "Folder")
                Walk(character, c, depth + 1, container, acc, seen, out);
        }
        return;
    }

    for (const auto& c : rbx::children(node))
        Walk(character, c, depth + 1, container, under_acc, seen, out);
}

} // namespace

std::vector<Entry> Collect(std::uintptr_t character)
{
    std::vector<Entry> out;
    if (!mem::is_valid(character)) return out;
    out.reserve(48);
    std::unordered_set<std::uintptr_t> seen;
    Walk(character, character, 0, {}, false, seen, out);
    return out;
}

std::vector<Entry> CollectDrawable(std::uintptr_t character)
{
    std::vector<Entry> all = Collect(character);
    std::vector<Entry> out;
    out.reserve(all.size());
    for (auto& e : all)
    {
        if (!e.part || !mem::is_valid(e.part)) continue;
        if (IsSkipPartName(e.name)) continue;
        if (e.kind == Kind::Other) continue;
        out.push_back(std::move(e));
    }
    return out;
}

std::vector<Entry> CollectForBounds(std::uintptr_t character)
{
    std::vector<Entry> all = Collect(character);
    std::vector<Entry> out;
    out.reserve(all.size());
    for (auto& e : all)
    {
        if (!e.part || !mem::is_valid(e.part)) continue;
        if (IsSkipPartName(e.name)) continue;
        out.push_back(std::move(e));
    }
    return out;
}

} // namespace mesh_parser

// ---- mesh chams (fill-only port) ----------------------------------------

namespace meshes {
namespace {

bool ReadCFrame(std::uintptr_t part, Vec3& out_pos, float out_rot[9], Vec3& out_sz)
{
    if (!mem::is_valid(part)) return false;
    const std::uintptr_t prim = mem::read<std::uintptr_t>(part + off::Primitive);
    if (!mem::is_valid(prim)) return false;

    const bool contiguous = off::Size == (off::Position + 12) &&
                            off::Rotation == (off::Position + 24);
    if (contiguous)
    {
        struct PrimBlock { float pos[3]; float sz[3]; float rot[9]; };
        PrimBlock blk{};
        if (mem::read_block(prim + off::Position, &blk, sizeof(blk)))
        {
            for (int i = 0; i < 9; ++i)
                out_rot[i] = std::isfinite(blk.rot[i]) ? blk.rot[i] : (i % 4 == 0) ? 1.f : 0.f;
            out_pos = Vec3{ blk.pos[0], blk.pos[1], blk.pos[2] };
            out_sz  = Vec3{ blk.sz[0],  blk.sz[1],  blk.sz[2] };
            return std::isfinite(out_pos.x) && std::isfinite(out_pos.y) && std::isfinite(out_pos.z);
        }
    }

    if (off::Rotation)
    {
        float rot[9]{ 1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f };
        mem::read_block(prim + off::Rotation, rot, 9 * sizeof(float));
        for (int i = 0; i < 9; ++i)
            out_rot[i] = std::isfinite(rot[i]) ? rot[i] : (i % 4 == 0) ? 1.f : 0.f;
    }
    else
    {
        for (int i = 0; i < 9; ++i) out_rot[i] = (i % 4 == 0) ? 1.f : 0.f;
    }

    out_pos = mem::read<Vec3>(prim + off::Position);
    out_sz = off::Size ? mem::read<Vec3>(prim + off::Size) : Vec3{ 1.f, 1.f, 1.f };
    return std::isfinite(out_pos.x) && std::isfinite(out_pos.y) && std::isfinite(out_pos.z);
}

bool W2S(const Mat4& m, float sw, float sh, const Vec3& p, ImVec2& out)
{
    Vec2 s{};
    if (!rbx::w2s(p, s, m, sw, sh)) return false;
    out = ImVec2{ s.x, s.y };
    return true;
}

int BodyPartIndex(const std::string& name)
{
    if (name == "Torso" || name == "UpperTorso" || name == "LowerTorso") return 1;
    if (name == "Left Arm" || name == "LeftUpperArm" || name == "LeftLowerArm" || name == "LeftHand") return 2;
    if (name == "Right Arm" || name == "RightUpperArm" || name == "RightLowerArm" || name == "RightHand") return 3;
    if (name == "Left Leg" || name == "LeftUpperLeg" || name == "LeftLowerLeg" || name == "LeftFoot") return 4;
    if (name == "Right Leg" || name == "RightUpperLeg" || name == "RightLowerLeg" || name == "RightFoot") return 5;
    return -1;
}

std::string FindCharacterMeshId(std::uintptr_t character, int body_part)
{
    if (body_part < 0 || !mem::is_valid(character)) return {};
    for (const auto& c : rbx::children(character))
    {
        if (rbx::classname(c) != "CharacterMesh") continue;
        const int bp = mem::read<int>(c + moff::CharacterMesh_BodyPart);
        if (bp != body_part) continue;
        const std::string id = read_content_string(c + moff::CharacterMesh_MeshId);
        if (!id.empty()) return id;
    }
    return {};
}

struct ResolveResult {
    std::string mesh_id;
    Vec3 scale{ 1.f, 1.f, 1.f };
    Vec3 offset{ 0.f, 0.f, 0.f };
    bool fit_to_part{ false };
    bool is_special{ false };
};

ResolveResult Resolve(const mesh_parser::Entry& e)
{
    ResolveResult r{};
    if (!e.part) return r;

    if (e.class_name == "MeshPart")
    {
        r.mesh_id = read_content_string(e.part + moff::MeshPart_MeshId);
        if (r.mesh_id == "Unknown") r.mesh_id.clear();
        r.fit_to_part = true;
    }

    if (e.special_mesh)
    {
        r.is_special = true;
        if (r.mesh_id.empty())
        {
            r.mesh_id = read_content_string(e.special_mesh + moff::SpecialMesh_MeshId);
            if (r.mesh_id == "Unknown") r.mesh_id.clear();
        }
        r.scale = mem::read<Vec3>(e.special_mesh + moff::SpecialMesh_Scale);
        r.offset = mem::read<Vec3>(e.special_mesh + moff::SpecialMesh_Offset);
        auto clamp_sc = [](float& v) {
            if (!std::isfinite(v) || v < 1e-4f) v = 1.f;
            if (v > 50.f) v = 50.f;
        };
        clamp_sc(r.scale.x); clamp_sc(r.scale.y); clamp_sc(r.scale.z);
        r.fit_to_part = false;
    }

    if (r.mesh_id.empty() && !e.mesh_id.empty())
        r.mesh_id = e.mesh_id;

    // R6 CharacterMesh overlay on limbs
    if (r.mesh_id.empty() && e.kind == mesh_parser::Kind::Body)
    {
        r.mesh_id = FindCharacterMeshId(e.character, BodyPartIndex(e.name));
        if (!r.mesh_id.empty())
            r.fit_to_part = true;
    }

    // Head: classic head.mesh fallback
    if (e.name == "Head")
    {
        const bool have = !r.mesh_id.empty() && (bool)MeshCache::Get().FindShared(r.mesh_id);
        if (!have)
        {
            r.mesh_id = "rbxasset://avatar/heads/head.mesh";
            r.fit_to_part = false;
            if (!r.is_special)
            {
                r.scale = { 1.25f, 1.25f, 1.25f };
                r.offset = { 0.f, 0.f, 0.f };
            }
        }
        else if (r.is_special)
        {
            r.fit_to_part = false;
        }
    }

    return r;
}

std::shared_ptr<const CachedMesh> LookupMesh(const std::string& mesh_id)
{
    if (mesh_id.empty()) return nullptr;
    return MeshCache::Get().FindShared(mesh_id);
}

bool IsAccessoryKind(mesh_parser::Kind k)
{
    return k == mesh_parser::Kind::Accessory ||
           k == mesh_parser::Kind::Hair ||
           k == mesh_parser::Kind::Face;
}

bool MeshAabb(const CachedMesh& mesh, const Vec3& ms, float out_min[3], float out_max[3])
{
    out_min[0] = out_min[1] = out_min[2] = FLT_MAX;
    out_max[0] = out_max[1] = out_max[2] = -FLT_MAX;
    const int n = (int)mesh.vertices.size();
    if (n <= 0) return false;
    int step = 1;
    if (n > 16000) step = (n + 15999) / 16000;
    for (int i = 0; i < n; i += step)
    {
        const float* p = mesh.vertices[i].pos;
        if (!std::isfinite(p[0]) || !std::isfinite(p[1]) || !std::isfinite(p[2])) continue;
        const float px = p[0] * ms.x, py = p[1] * ms.y, pz = p[2] * ms.z;
        out_min[0] = (std::min)(out_min[0], px); out_max[0] = (std::max)(out_max[0], px);
        out_min[1] = (std::min)(out_min[1], py); out_max[1] = (std::max)(out_max[1], py);
        out_min[2] = (std::min)(out_min[2], pz); out_max[2] = (std::max)(out_max[2], pz);
    }
    if (n > 1)
    {
        for (int i : { 0, n - 1 })
        {
            const float* p = mesh.vertices[i].pos;
            if (!std::isfinite(p[0])) continue;
            const float px = p[0] * ms.x, py = p[1] * ms.y, pz = p[2] * ms.z;
            out_min[0] = (std::min)(out_min[0], px); out_max[0] = (std::max)(out_max[0], px);
            out_min[1] = (std::min)(out_min[1], py); out_max[1] = (std::max)(out_max[1], py);
            out_min[2] = (std::min)(out_min[2], pz); out_max[2] = (std::max)(out_max[2], pz);
        }
    }
    return out_min[0] <= out_max[0];
}

bool IsClassicHeadMesh(const std::string& id)
{
    return id.find("heads/head.mesh") != std::string::npos ||
           CleanAssetId(id) == "head.mesh";
}

void ApplyVisualFit(const mesh_parser::Entry& e, const ResolveResult& rr,
                    const CachedMesh& mesh, Vec3& ms, Vec3& off, const Vec3& sz)
{
    if (rr.is_special)
    {
        ms = rr.scale;
        off = rr.offset;
        return;
    }

    if (e.name == "Head" && IsClassicHeadMesh(rr.mesh_id))
    {
        float sy = rr.scale.y;
        if (!std::isfinite(sy) || sy < 0.25f || sy > 3.f) sy = 1.25f;
        ms = { sy, sy, sy };
        off = { 0.f, 0.f, 0.f };
        return;
    }

    if (rr.fit_to_part && sz.x > 0.01f && sz.y > 0.01f && sz.z > 0.01f)
    {
        if (e.class_name == "MeshPart")
        {
            ms = { 1.f, 1.f, 1.f };
            off = { 0.f, 0.f, 0.f };
            return;
        }

        float mn[3], mx[3];
        if (MeshAabb(mesh, { 1.f, 1.f, 1.f }, mn, mx))
        {
            const float ax = mx[0] - mn[0], ay = mx[1] - mn[1], az = mx[2] - mn[2];
            if (ax > 1e-4f && ay > 1e-4f && az > 1e-4f)
            {
                float rx = sz.x / ax, ry = sz.y / ay, rz = sz.z / az;
                if (rx > 0.85f && rx < 1.15f && ry > 0.85f && ry < 1.15f && rz > 0.85f && rz < 1.15f)
                    ms = { 1.f, 1.f, 1.f };
                else
                {
                    if (rx < 0.01f) rx = 0.01f; if (rx > 50.f) rx = 50.f;
                    if (ry < 0.01f) ry = 0.01f; if (ry > 50.f) ry = 50.f;
                    if (rz < 0.01f) rz = 0.01f; if (rz > 50.f) rz = 50.f;
                    ms = { rx, ry, rz };
                }
            }
            else
                ms = { 1.f, 1.f, 1.f };
        }
        else
            ms = { 1.f, 1.f, 1.f };
        off = { 0.f, 0.f, 0.f };
        return;
    }

    ms = rr.scale;
    off = rr.offset;
}

Vec3 MakeWorld(const Vec3& pos, const float rot[9], const float lx, const float ly, const float lz)
{
    return {
        pos.x + rot[0] * lx + rot[1] * ly + rot[2] * lz,
        pos.y + rot[3] * lx + rot[4] * ly + rot[5] * lz,
        pos.z + rot[6] * lx + rot[7] * ly + rot[8] * lz,
    };
}

void DrawBoxFallback(ImDrawList* dl, const Vec3& pos, const float rot[9], const Vec3& sz,
                     const Mat4& view, float sw, float sh, ImU32 fill)
{
    if (sz.x < 0.01f && sz.y < 0.01f && sz.z < 0.01f) return;
    const Vec3 h{ sz.x * 0.5f, sz.y * 0.5f, sz.z * 0.5f };
    const Vec3 lc[8] = {
        { -h.x, -h.y, -h.z }, { -h.x, -h.y,  h.z },
        { -h.x,  h.y, -h.z }, { -h.x,  h.y,  h.z },
        {  h.x, -h.y, -h.z }, {  h.x, -h.y,  h.z },
        {  h.x,  h.y, -h.z }, {  h.x,  h.y,  h.z },
    };
    ImVec2 sp[8];
    bool sv[8]{};
    bool any = false;
    for (int i = 0; i < 8; ++i)
    {
        const Vec3 wc = MakeWorld(pos, rot, lc[i].x, lc[i].y, lc[i].z);
        sv[i] = W2S(view, sw, sh, wc, sp[i]);
        any = any || sv[i];
    }
    if (!any) return;
    static const int tris[12][3] = {
        { 0, 1, 3 }, { 0, 3, 2 }, { 4, 6, 7 }, { 4, 7, 5 },
        { 0, 4, 5 }, { 0, 5, 1 }, { 2, 3, 7 }, { 2, 7, 6 },
        { 0, 2, 6 }, { 0, 6, 4 }, { 1, 5, 7 }, { 1, 7, 3 },
    };
    for (const auto& t : tris)
    {
        if (!sv[t[0]] || !sv[t[1]] || !sv[t[2]]) continue;
        const ImVec2& a = sp[t[0]];
        const ImVec2& b = sp[t[1]];
        const ImVec2& c = sp[t[2]];
        const float cr = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
        if (cr < 0.15f) continue;
        dl->AddTriangleFilled(a, b, c, fill);
    }
}

// ---- bounds caches (expand_bounds hot path) -------------------------------
// CollectForBounds re-walks the whole character tree (classname/name_of/
// children RPMs per node) and Resolve/ApplyVisualFit issue string + mesh reads.
// Caching per character/part at a few Hz keeps that cost off the render loop —
// the per-frame work left is just 1-2 CFrame RPMs plus the W2S projection.

struct PartsCache {
    ULONGLONG t{ 0 };
    std::vector<mesh_parser::Entry> parts;
};

struct BoundsSolve {
    ULONGLONG t{ 0 };
    bool valid{ false };
    ResolveResult rr;
    std::shared_ptr<const CachedMesh> mesh;
    Vec3 ms{ 1.f, 1.f, 1.f };
    Vec3 off{ 0.f, 0.f, 0.f };
};

static std::unordered_map<std::uintptr_t, PartsCache>   s_parts_exp;
static std::unordered_map<std::uintptr_t, BoundsSolve>  s_bounds_solve;
static ULONGLONG s_bounds_cleared = 0;

} // namespace

bool expand_bounds(uintptr_t character, const Mat4& view, float sw, float sh,
                   float& min_x, float& max_x, float& min_y, float& max_y,
                   Vec3& wmin, Vec3& wmax)
{
    if (!mem::is_valid(character)) return false;

    MeshCache::Get().Refresh(false);

    const ULONGLONG now = GetTickCount64();

    // periodically drop stale entries so the per-part solve table can't grow
    // without bound across long sessions / many characters.
    if (now - s_bounds_cleared > 2000ull)
    {
        s_bounds_cleared = now;
        s_parts_exp.clear();
        s_bounds_solve.clear();
    }

    auto& pc = s_parts_exp[character];
    if (pc.parts.empty() || now - pc.t > 250ull)
    {
        pc.parts = mesh_parser::CollectForBounds(character);
        pc.t = now;
    }
    const auto& parts = pc.parts;
    if (parts.empty()) return false;

    bool any = false;

    auto push_screen = [&](const Vec3& p) {
        ImVec2 sc{};
        if (W2S(view, sw, sh, p, sc))
        {
            min_x = (std::min)(min_x, sc.x); max_x = (std::max)(max_x, sc.x);
            min_y = (std::min)(min_y, sc.y); max_y = (std::max)(max_y, sc.y);
            any = true;
        }
    };

    for (const auto& e : parts)
    {
        Vec3 pos{}, sz{};
        float rot[9]{};
        if (!ReadCFrame(e.part, pos, rot, sz)) continue;

        BoundsSolve& bs = s_bounds_solve[e.part];
        if (!bs.valid || now - bs.t > 500ull)
        {
            bs = BoundsSolve{};
            bs.rr = Resolve(e);
            bs.t = now;
            bs.mesh = LookupMesh(bs.rr.mesh_id);
            bs.ms = bs.rr.scale;
            bs.off = bs.rr.offset;
            if (bs.mesh && !bs.mesh->vertices.empty())
                ApplyVisualFit(e, bs.rr, *bs.mesh, bs.ms, bs.off, sz);
            bs.valid = true;
        }

        // part OBB always contributes
        const Vec3 h{ sz.x * 0.5f, sz.y * 0.5f, sz.z * 0.5f };
        const Vec3 lc[8] = {
            { -h.x, -h.y, -h.z }, { -h.x, -h.y,  h.z },
            { -h.x,  h.y, -h.z }, { -h.x,  h.y,  h.z },
            {  h.x, -h.y, -h.z }, {  h.x, -h.y,  h.z },
            {  h.x,  h.y, -h.z }, {  h.x,  h.y,  h.z },
        };
        for (const auto& c : lc)
        {
            const Vec3 wc = MakeWorld(pos, rot, c.x, c.y, c.z);
            wmin.x = (std::min)(wmin.x, wc.x); wmax.x = (std::max)(wmax.x, wc.x);
            wmin.y = (std::min)(wmin.y, wc.y); wmax.y = (std::max)(wmax.y, wc.y);
            wmin.z = (std::min)(wmin.z, wc.z); wmax.z = (std::max)(wmax.z, wc.z);
            push_screen(wc);
        }

        if (!bs.mesh || bs.mesh->vertices.empty()) continue;

        const CachedMesh& mesh = *bs.mesh;
        const Vec3& ms = bs.ms;
        const Vec3& off = bs.off;

        const int n = (int)mesh.vertices.size();
        int step = n / 96;
        if (step < 1) step = 1;
        for (int i = 0; i < n; i += step)
        {
            const float* p = mesh.vertices[i].pos;
            if (!std::isfinite(p[0]) || !std::isfinite(p[1]) || !std::isfinite(p[2])) continue;
            const float lx = p[0] * ms.x + off.x;
            const float ly = p[1] * ms.y + off.y;
            const float lz = p[2] * ms.z + off.z;
            const Vec3 wc = MakeWorld(pos, rot, lx, ly, lz);
            wmin.x = (std::min)(wmin.x, wc.x); wmax.x = (std::max)(wmax.x, wc.x);
            wmin.y = (std::min)(wmin.y, wc.y); wmax.y = (std::max)(wmax.y, wc.y);
            wmin.z = (std::min)(wmin.z, wc.z); wmax.z = (std::max)(wmax.z, wc.z);
            push_screen(wc);
        }
    }

    return any;
}

void draw_mesh(ImDrawList* dl, uintptr_t character, const Mat4& view,
               float sw, float sh, ImU32 fill_col)
{
    if (!mem::is_valid(character) || !dl)
        return;

    MeshCache::Get().Refresh(false);

    const ULONGLONG now = GetTickCount64();

    struct PartsCache {
        ULONGLONG t{ 0 };
        std::vector<mesh_parser::Entry> parts;
    };
    static std::unordered_map<std::uintptr_t, PartsCache> s_parts;
    {
        auto& pc = s_parts[character];
        if (pc.parts.empty() || now - pc.t > 250ull)
        {
            pc.parts = mesh_parser::CollectDrawable(character);
            pc.t = now;
        }
        if (pc.parts.empty())
            return;
    }
    const auto& parts = s_parts[character].parts;

    struct CachedResolve {
        ResolveResult r;
    };
    static std::unordered_map<std::uintptr_t, CachedResolve> s_resolve;
    static ULONGLONG s_cache_t = 0;
    if (now - s_cache_t > 750ull)
    {
        s_resolve.clear();
        s_cache_t = now;
    }

    ImDrawListFlags bak = dl->Flags;
    dl->Flags &= ~ImDrawListFlags_AntiAliasedFill;

    bool need_force_mcp = false;

    for (const auto& e : parts)
    {
        Vec3 pos{}, sz{};
        float rot[9]{};
        if (!ReadCFrame(e.part, pos, rot, sz))
            continue;

        const bool is_acc = IsAccessoryKind(e.kind);

        ResolveResult rr;
        auto it = s_resolve.find(e.part);
        if (it != s_resolve.end() && !it->second.r.mesh_id.empty() && !is_acc)
            rr = it->second.r;
        else
        {
            rr = Resolve(e);
            if (!rr.mesh_id.empty())
                s_resolve[e.part] = { rr };
            else
                s_resolve.erase(e.part);
        }

        if (e.name == "Head")
        {
            std::string real;
            if (e.special_mesh)
                real = read_content_string(e.special_mesh + moff::SpecialMesh_MeshId);
            else if (e.class_name == "MeshPart")
                real = read_content_string(e.part + moff::MeshPart_MeshId);
            if (real == "Unknown") real.clear();

            if (!real.empty() && MeshCache::Get().FindShared(real))
            {
                rr.mesh_id = real;
                if (e.special_mesh)
                {
                    rr.fit_to_part = false;
                    rr.is_special = true;
                    rr.scale = mem::read<Vec3>(e.special_mesh + moff::SpecialMesh_Scale);
                    rr.offset = mem::read<Vec3>(e.special_mesh + moff::SpecialMesh_Offset);
                    auto clamp_sc = [](float& v) {
                        if (!std::isfinite(v) || v < 1e-4f) v = 1.f;
                        if (v > 50.f) v = 50.f;
                    };
                    clamp_sc(rr.scale.x); clamp_sc(rr.scale.y); clamp_sc(rr.scale.z);
                }
                else
                {
                    rr.fit_to_part = true;
                    rr.scale = { 1.f, 1.f, 1.f };
                    rr.offset = { 0.f, 0.f, 0.f };
                }
                s_resolve[e.part] = { rr };
            }
            else if (!real.empty())
            {
                need_force_mcp = true;
            }
        }

        auto mesh = LookupMesh(rr.mesh_id);
        if (!mesh || mesh->faces.empty())
        {
            if (!rr.mesh_id.empty())
                need_force_mcp = true;

            mesh = LookupMesh("rbxasset://avatar/heads/head.mesh");
            if (e.name == "Head" && mesh && !mesh->faces.empty())
            {
                rr.mesh_id = "rbxasset://avatar/heads/head.mesh";
                rr.fit_to_part = false;
                rr.scale = { 1.25f, 1.25f, 1.25f };
                rr.offset = { 0.f, 0.f, 0.f };
            }
            else
            {
                if (!is_acc && e.name != "Head")
                    DrawBoxFallback(dl, pos, rot, sz, view, sw, sh, fill_col);
                continue;
            }
        }

        Vec3 ms = rr.scale;
        Vec3 off = rr.offset;
        ApplyVisualFit(e, rr, *mesh, ms, off, sz);

        const int vtx_count = (int)mesh->vertices.size();
        if (vtx_count <= 0 || vtx_count > 50000) continue;

        const int fac_total = (int)mesh->faces.size();
        if (fac_total <= 0) continue;

        int fac_stride = 1;
        const int fac_budget = 12000;
        if (fac_total > fac_budget)
            fac_stride = (fac_total + fac_budget - 1) / fac_budget;

        static std::vector<ImVec2> s_screen;
        static std::vector<std::uint8_t> s_need;
        static std::vector<std::uint8_t> s_ok;
        if (s_need.size() < (std::size_t)vtx_count) s_need.resize((std::size_t)vtx_count, 0);
        if (s_screen.size() < (std::size_t)vtx_count) s_screen.resize((std::size_t)vtx_count);
        if (s_ok.size() < (std::size_t)vtx_count) s_ok.resize((std::size_t)vtx_count, 0);
        s_need.assign((std::size_t)vtx_count, 0);
        s_ok.assign((std::size_t)vtx_count, 0);
        std::uint8_t* need = s_need.data();
        ImVec2* screen = s_screen.data();
        std::uint8_t* ok = s_ok.data();
        for (int i = 0; i < fac_total; i += fac_stride)
        {
            const auto& f = mesh->faces[i];
            if (f.indices[0] < (std::uint32_t)vtx_count) need[f.indices[0]] = 1;
            if (f.indices[1] < (std::uint32_t)vtx_count) need[f.indices[1]] = 1;
            if (f.indices[2] < (std::uint32_t)vtx_count) need[f.indices[2]] = 1;
        }

        for (int i = 0; i < vtx_count; ++i)
        {
            if (!need[i]) continue;
            const float* p = mesh->vertices[i].pos;
            const float lx = p[0] * ms.x + off.x;
            const float ly = p[1] * ms.y + off.y;
            const float lz = p[2] * ms.z + off.z;
            const Vec3 wc = MakeWorld(pos, rot, lx, ly, lz);
            if (!W2S(view, sw, sh, wc, screen[i]))
                continue;
            if (screen[i].x < -sw || screen[i].x > sw * 2.f ||
                screen[i].y < -sh || screen[i].y > sh * 2.f)
                continue;
            ok[i] = 1;
        }

        for (int i = 0; i < fac_total; i += fac_stride)
        {
            const std::uint32_t i0 = mesh->faces[i].indices[0];
            const std::uint32_t i1 = mesh->faces[i].indices[1];
            const std::uint32_t i2 = mesh->faces[i].indices[2];
            if (i0 >= (std::uint32_t)vtx_count || i1 >= (std::uint32_t)vtx_count ||
                i2 >= (std::uint32_t)vtx_count)
                continue;
            if (!ok[i0] || !ok[i1] || !ok[i2])
                continue;

            const ImVec2& a = screen[i0];
            const ImVec2& b = screen[i1];
            const ImVec2& c = screen[i2];
            const float cross = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
            if (cross < 0.15f)
                continue;

            dl->AddTriangleFilled(a, b, c, fill_col);
        }
    }

    if (need_force_mcp)
    {
        static ULONGLONG s_force_mcp = 0;
        if (now - s_force_mcp > 400ull)
        {
            s_force_mcp = now;
            MeshCache::Get().Refresh(true);
        }
    }

    dl->Flags = bak;
}

} // namespace meshes