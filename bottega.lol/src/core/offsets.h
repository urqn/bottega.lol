// bottega.lol live offsets
#pragma once
#include <cstdint>
#include <map>
#include <string>

namespace off {

    inline std::map<std::string, uintptr_t> map;
    inline std::string version;

    inline uintptr_t FakeDataModelPtr = 0;
    inline uintptr_t FakeToReal       = 0;
    inline uintptr_t VisualEnginePtr  = 0;
    inline uintptr_t ViewMatrix       = 0;
    inline uintptr_t ViewportSz       = 0;
inline uintptr_t Camera           = 0;
inline uintptr_t CameraPos        = 0;
inline uintptr_t CameraViewport   = 0;
inline uintptr_t CameraRotation   = 0;
    inline uintptr_t LocalPlayer      = 0;
    inline uintptr_t NameContainer    = 0;
    inline uintptr_t Name             = 0;
    inline uintptr_t ClassDesc        = 0;
    inline uintptr_t ClassName        = 0;
    inline uintptr_t Children         = 0;
    inline uintptr_t ChildrenEnd      = 0;
    inline uintptr_t TeamColor        = 0;
    inline uintptr_t PlayerDisplayName = 0;
    inline uintptr_t ModelInstance    = 0;
    inline uintptr_t Health           = 0;
    inline uintptr_t MaxHealth        = 0;
    inline uintptr_t Primitive        = 0;
    inline uintptr_t Position         = 0;
    inline uintptr_t Velocity         = 0;
    inline uintptr_t Rotation         = 0;
    inline uintptr_t RigType          = 0;
    inline uintptr_t Size             = 0;

    // exact-case lookup into the published map, e.g. off::find(xs("Humanoid.WalkSpeed"))
    inline uintptr_t find(const std::string& key)
    {
        const auto it = map.find(key);
        return (it == map.end()) ? 0 : it->second;
    }

    bool fetch(const std::string& client_version);
}
