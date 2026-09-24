#pragma once
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <windows.h>
#include "imgui.h"
#include "vec.h"

#pragma pack(push, 1)
struct MeshVertex {
    float pos[3];
    float normal[3];
    float uv[2];
    std::uint32_t tangent;
    std::uint32_t color;
};
static_assert(sizeof(MeshVertex) == 40, "MeshVertex");

struct MeshFace {
    std::uint32_t indices[3];
};
static_assert(sizeof(MeshFace) == 12, "MeshFace");
#pragma pack(pop)

struct CachedMesh {
    std::string asset_id;
    std::vector<MeshVertex> vertices;
    std::vector<MeshFace> faces;
};

// memory-only: MeshContentProvider LRU -> MeshData (no HTTP/API)
class MeshCache {
public:
    static MeshCache& Get();

    void Refresh(bool force = false);
    std::shared_ptr<const CachedMesh> FindShared(const std::string& asset_id) const;
    std::size_t Count() const;

private:
    MeshCache() = default;
    mutable std::mutex mtx_;
    std::unordered_map<std::string, std::shared_ptr<CachedMesh>> cache_;
    ULONGLONG last_refresh_{ 0 };
};

std::string CleanAssetId(const std::string& raw);

namespace mesh_parser {

enum class Kind : std::uint8_t {
    Body = 0,
    Accessory = 1,
    Face = 2,
    Hair = 3,
    CharacterMesh = 4,
    Special = 5,
    Other = 6
};

struct Entry {
    std::uintptr_t part{ 0 };          // BasePart / Handle (drawing target)
    std::uintptr_t special_mesh{ 0 };  // SpecialMesh child, if any
    std::uintptr_t character{ 0 };
    Kind kind{ Kind::Other };
    std::string name;
    std::string class_name;
    std::string mesh_id;
    std::string container;
};

// all visual meshes of a character: body + accessory/hair/face + SpecialMesh
std::vector<Entry> Collect(std::uintptr_t character);
std::vector<Entry> CollectDrawable(std::uintptr_t character);
std::vector<Entry> CollectForBounds(std::uintptr_t character);

} // namespace mesh_parser

namespace meshes {

// phantomX MeshChams::Draw fill path only (no shader / no contour outline).
void draw_mesh(ImDrawList* dl, std::uintptr_t character,
               const Mat4& view, float sw, float sh, ImU32 fill_col);

// phantomX MeshChams::ExpandBounds: screen/world AABB from real meshes.
bool expand_bounds(std::uintptr_t character,
                   const Mat4& view, float sw, float sh,
                   float& min_x, float& max_x, float& min_y, float& max_y,
                   Vec3& wmin, Vec3& wmax);

} // namespace meshes