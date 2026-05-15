#pragma once
//
// offsets.hpp — CS2 (Source 2) memory offsets — dump 2026-05-14
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
    constexpr uintptr_t dwEntityList  = 0x24D4E80;

    // Pointer to the local CCSPlayerController.
    constexpr uintptr_t dwLocalPlayer = 0x230E5C0;

    // Flat 4×4 float view-projection matrix (64 bytes, 16 floats).
    // Updated every frame by the renderer — safe to read each tick.
    constexpr uintptr_t dwViewMatrix  = 0x2334850;
}

// ── CEntityIdentity (entity list entry) ──────────────────────────────────────
// In CS2 (Source 2), each chunk holds CEntityIdentity structs contiguously.
// Layout (see CounterStrikeSharp EntitySystem):
//   +0x00  CEntityInstance*  (the live entity object)
//   +0x10  CEntityHandle     (must match the slot index before trusting +0x00)
namespace EntityIdentity
{
    constexpr uintptr_t pEntity = 0x00;
    constexpr uintptr_t pHandle = 0x10;

    // sizeof(CEntityIdentity) — stride between consecutive entries in a chunk.
    constexpr int kStride = 0x70;
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
    constexpr uintptr_t pChunks              = 0x10;   // inline chunk-ptr array start within CGameEntitySystem
    constexpr uintptr_t highestEntityIndex   = 0x2090; // int32 — last used entity index
    constexpr int       kChunkSize           = 512;    // entities per chunk
    constexpr int       kMaxPlayerSlots      = 64;     // CS2 player controller slots
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

    // ── Batch-read optimisation ───────────────────────────────────────────────
    // Reading every pawn field individually costs 4+ RPM syscalls.
    // Instead we read the span [m_pGameSceneNode .. m_iTeamNum] in one call.
    //
    //   Base offset          : 0x330  (m_pGameSceneNode)
    //   End  offset          : 0x3EB  (m_iTeamNum)
    //   Buffer size          : 0xBC   (188 bytes)
    //
    // Field offsets *within* the 0xBC buffer:
    constexpr uintptr_t kBatchBase        = m_pGameSceneNode;       // 0x330
    constexpr size_t    kBatchSize        = m_iTeamNum - kBatchBase + 1; // 0xBC

    constexpr size_t    kOff_SceneNode    = m_pGameSceneNode - kBatchBase; // 0x000
    constexpr size_t    kOff_Health       = m_iHealth        - kBatchBase; // 0x01C
    constexpr size_t    kOff_LifeState    = m_lifeState      - kBatchBase; // 0x024
    constexpr size_t    kOff_TeamNum      = m_iTeamNum       - kBatchBase; // 0x0BB
}

// ── CGameSceneNode / CSkeletonInstance ───────────────────────────────────────
// Reached by dereferencing pawn + Pawn::m_pGameSceneNode.
// For player pawns the pointer actually points to a CSkeletonInstance, which
// inherits from CGameSceneNode and carries the bone matrix array.
namespace GameSceneNode
{
    // Vector3 (3 × float) — absolute world-space position.
    constexpr uintptr_t m_vecAbsOrigin = 0xC8;

    // bool — true when the entity is outside the local player's PVS.
    constexpr uintptr_t m_bDormant     = 0x103;

    // CSkeletonInstance::m_modelState (embedded CModelState) starts here.
    constexpr uintptr_t m_modelState   = 0x150;

    // Offset of the bone world-transform pointer inside CModelState.
    // Bone array pointer = sceneNode + m_modelState + m_boneArray
    constexpr uintptr_t m_boneArray    = 0x80;
}

// ── Bone data ────────────────────────────────────────────────────────────────
// Each entry in the bone array is 32 bytes (0x20):
//   +0x00  quaternion  (float[4], 16 bytes)  — rotation
//   +0x10  Vec3        (float[3], 12 bytes)  — world-space position  ← use this
//   +0x1C  float                             — uniform scale
namespace Bones
{
    constexpr uintptr_t kStride    = 0x20;  // bytes between consecutive bone entries
    constexpr uintptr_t kPosOffset = 0x10;  // Vec3 world position starts after the quaternion

    // Total bones to read (max used index + 1 for CS2 standard player model).
    constexpr int kCount = 22;

    // ── Bone indices — standard CS2 T/CT player model ────────────────────────
    // Hierarchy: root(0) → pelvis(1) → spine chain(2-4) → neck(5) → head(6)
    //            chest(4) → clavicles(7,11) → upper arms(8,12) → lower arms(9,13) → hands(10,14)
    //            pelvis(1) → upper legs(15,19) → lower legs(16,20) → ankles(17,21)
    constexpr int HEAD       = 6;
    constexpr int NECK       = 5;
    constexpr int CHEST      = 4;   // spine_2
    constexpr int SPINE      = 2;   // spine_0
    constexpr int PELVIS     = 1;   // pelvis (index 0 is root — skip it)
    constexpr int LSHOULDER  = 7;   // clavicle_L
    constexpr int LELBOW     = 8;   // arm_upper_L
    constexpr int LWRIST     = 9;   // arm_lower_L
    constexpr int RSHOULDER  = 11;  // clavicle_R
    constexpr int RELBOW     = 12;  // arm_upper_R
    constexpr int RWRIST     = 13;  // arm_lower_R
    constexpr int LHIP       = 15;  // leg_upper_L
    constexpr int LKNEE      = 16;  // leg_lower_L
    constexpr int LANKLE     = 17;  // ankle_L
    constexpr int RHIP       = 19;  // leg_upper_R
    constexpr int RKNEE      = 20;  // leg_lower_R
    constexpr int RANKLE     = 21;  // ankle_R
}

// ── Weapon reading ────────────────────────────────────────────────────────────
// Chain: C_CSPlayerPawn → CPlayer_WeaponServices → active weapon handle →
//        weapon entity (C_EconEntity) → C_AttributeContainer → C_EconItemView
//        → m_iItemDefinitionIndex
namespace Weapon
{
    // C_CSPlayerPawn::m_pWeaponServices — pointer to CPlayer_WeaponServices
    constexpr uintptr_t m_pWeaponServices = 0x11E0;

    // CPlayer_WeaponServices::m_hActiveWeapon — CHandle<C_CSWeaponBase>
    constexpr uintptr_t m_hActiveWeapon   = 0x60;

    // From the weapon entity base:
    //   C_EconEntity::m_AttributeManager   at 0x1180  (embedded struct)
    //   C_AttributeContainer::m_Item        at 0x50   (embedded struct)
    //   C_EconItemView::m_iItemDefinitionIndex at 0x1BA  (uint16)
    constexpr uintptr_t m_iItemDefinitionIndex =
        0x1180 + 0x50 + 0x1BA;  // = 0x13EA from weapon entity base
}

// ── Handle encoding ───────────────────────────────────────────────────────────
// Source 2 entity handles pack {serialNumber:12, entityIndex:20} into 32 bits.
inline int HandleToIndex(uint32_t handle)
{
    return static_cast<int>(handle & 0x7FFF); // low 15 bits = entity index
}

} // namespace Offsets
