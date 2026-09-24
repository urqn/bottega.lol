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

    // exact-case lookup into the published map, e.g. off::find(xs("Humanoid.WalkSpeed")).
    // the map is keyed by the property name (the leaf after the first dot of the
    // published path), so a dotted path is resolved to its leaf as a fallback.
    inline uintptr_t find(const std::string& key)
    {
        const auto it = map.find(key);
        if (it != map.end()) return it->second;
        const size_t dot = key.find('.');
        if (dot != std::string::npos && dot + 1 < key.size())
        {
            const auto it2 = map.find(key.substr(dot + 1));
            if (it2 != map.end()) return it2->second;
        }
        return 0;
    }

    bool fetch(const std::string& client_version);
}
