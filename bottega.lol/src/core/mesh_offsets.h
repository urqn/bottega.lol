// Mesh subsystem offsets (hardcoded, NOT part of the published offset DB).
//
// These values are ported from the PhantomX source, which targets Roblox
// version-c5aecda2245e4fae. bottega.lol currently targets Fishstrap
// version-2366ba214ec740ca, so this layout is UNVERIFIED on this client.
//
// All mesh reads are length/range bounded in mesh_esp.cpp: if the offsets
// don't match the running client, the mesh cache stays empty and the mesh
// renderer degrades to per-part OBB fills (no crash).
#pragma once
#include <cstdint>

namespace moff {

    inline constexpr std::uintptr_t MeshData_VertexStart  = 0x00;
    inline constexpr std::uintptr_t MeshData_VertexEnd    = 0x08;
    inline constexpr std::uintptr_t MeshData_FaceStart    = 0x30;
    inline constexpr std::uintptr_t MeshData_FaceEnd      = 0x38;

    inline constexpr std::uintptr_t MeshContentProvider_AssetID    = 0x10;
    inline constexpr std::uintptr_t MeshContentProvider_Cache      = 0xd8;
    inline constexpr std::uintptr_t MeshContentProvider_LRUCache   = 0x20;
    inline constexpr std::uintptr_t MeshContentProvider_ToMeshData = 0x40;
    inline constexpr std::uintptr_t MeshContentProvider_MeshData   = 0x40;

    inline constexpr std::uintptr_t MeshPart_MeshId     = 0x310;
    inline constexpr std::uintptr_t SpecialMesh_MeshId  = 0xf8;
    inline constexpr std::uintptr_t SpecialMesh_Scale   = 0xc4;
    inline constexpr std::uintptr_t SpecialMesh_Offset  = 0xb8;
    inline constexpr std::uintptr_t CharacterMesh_MeshId    = 0xf8;
    inline constexpr std::uintptr_t CharacterMesh_BodyPart  = 0x148;
}