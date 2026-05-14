#pragma once
//
// offsets.hpp — CS2 (Source 2) memory offsets
//
// ┌──────────────────────────────────────────────────────────────────────────┐
// │  EDUCATIONAL NOTE                                                        │
// │  Offsets shift with every game update. The values here are illustrative  │
// │  placeholders derived from publicly documented offset databases such as  │
// │  https://github.com/a2x/cs2-dumper  (MIT licensed).                     │
// │  Real values must be re-dumped after each CS2 update using a schema      │
// │  dumper (Hazedumper / cs2-dumper) or manual reverse engineering.         │
// └──────────────────────────────────────────────────────────────────────────┘
//
// CS2 Entity Model (Source 2):
//   CEntitySystem  →  CEntityIdentity[]  →  C_BaseEntity
//   CCSPlayerController  owns a handle (m_hPlayerPawn)  →  C_CSPlayerPawn
//   C_CSPlayerPawn holds actual in-game state (health, position, etc.)
//

#include <cstdint>

namespace Offsets {

// ── client.dll global pointers ───────────────────────────────────────────────
// These are relative to the base address of client.dll.

namespace Client
{
    // Pointer to CGameEntitySystem (the entity list root).
    constexpr uintptr_t dwEntityList  = 0x24D0DC0;

    // Pointer to the local CCSPlayerController.
    constexpr uintptr_t dwLocalPlayer = 0x230A4F0;

    // Flat 4×4 float view-projection matrix (64 bytes, 16 floats).
    // Updated every frame by the renderer — safe to read each tick.
    constexpr uintptr_t dwViewMatrix  = 0x2330AE0;
}

// ── CEntityIdentity (entity list entry) ──────────────────────────────────────
// In CS2 (Source 2), the entity list chunks hold CEntityIdentity *structs*
// contiguously — they are NOT arrays of pointers.  Each struct is 0x78 bytes.
namespace EntityIdentity
{
    // Offset of the entity C++ object pointer within CEntityIdentity.
    constexpr uintptr_t pObject = 0x10;

    // sizeof(CEntityIdentity) — stride between consecutive entries in a chunk.
    constexpr int kStride = 0x78;
}

// ── CEntitySystem / IEntityList chunk layout ──────────────────────────────────
// CS2 stores entities in a two-level array: an array of 512-entity chunks.
// Index formula:  chunk = entityIdx >> 9;  slot = entityIdx & 0x1FF
//
// IMPORTANT: pChunks is NOT a pointer stored at CGameEntitySystem+0x10.
// The chunk pointer array is EMBEDDED inline at that offset.  Compute the
// chunk array base as:  entitySystemAddr + pChunks  (no extra Read<ptr>).
namespace EntityList
{
    constexpr uintptr_t pChunks       = 0x10;   // inline chunk-ptr array start within CGameEntitySystem
    constexpr int       kChunkSize    = 512;     // entities per chunk
    constexpr int       kMaxEntities  = 512;     // full chunk scan (512 entities per chunk)
}

// ── CCSPlayerController ───────────────────────────────────────────────────────
// Networked controller object — owns the player's name and pawn handle.
namespace Controller
{
    // CHandle<C_CSPlayerPawn> — encode the entity index in the high 20 bits.
    constexpr uintptr_t m_hPlayerPawn   = 0x90C;
    // char[128] — UTF-8 display name.
    constexpr uintptr_t m_iszPlayerName = 0x6F4;
}

// ── C_CSPlayerPawn ────────────────────────────────────────────────────────────
// The actual in-game pawn: has physics, animation, health, position.
namespace Pawn
{
    // uint8_t  — team: 2 = T, 3 = CT.
    constexpr uintptr_t m_iTeamNum       = 0x3EB;

    // int32_t  — current HP (0–100 normally; can be >100 with armour kits).
    constexpr uintptr_t m_iHealth        = 0x34C;

    // uint8_t  — life state: 0 = alive, 1 = dying, 2 = dead.
    constexpr uintptr_t m_lifeState      = 0x354;

    // CGameSceneNode* — contains world-space position and dormancy flag.
    constexpr uintptr_t m_pGameSceneNode = 0x330;
}

// ── CGameSceneNode ────────────────────────────────────────────────────────────
// Reached by dereferencing pawn + Pawn::m_pGameSceneNode.
namespace GameSceneNode
{
    // Vector3 (3 × float) — absolute world-space position.
    constexpr uintptr_t m_vecAbsOrigin = 0xC8;

    // bool — true when the entity is outside the local player's PVS.
    constexpr uintptr_t m_bDormant     = 0x103;
}

// ── Handle encoding ───────────────────────────────────────────────────────────
// Source 2 entity handles pack {serialNumber:12, entityIndex:20} into 32 bits.
inline int HandleToIndex(uint32_t handle)
{
    return static_cast<int>(handle & 0x7FFF); // low 15 bits = entity index
}

} // namespace Offsets
