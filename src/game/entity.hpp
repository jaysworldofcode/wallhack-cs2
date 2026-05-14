#pragma once
//
// entity.hpp — Snapshot of a single CS2 player entity
//
// Instead of reading individual fields on demand (many small RPM calls),
// we take a full snapshot once per frame.  The EntityManager reads all
// relevant fields into a plain EntityData struct.  The renderer and
// the overlay then work entirely from these cached snapshots — zero
// additional ReadProcessMemory calls in the hot path.
//

#include <cstdint>
#include <string>
#include <vector>
#include <Windows.h>
#include <sstream>
#include <iomanip>

#include "offsets.hpp"
#include "../math/vec3.hpp"

// ── EntityData ───────────────────────────────────────────────────────────────

struct EntityData
{
    // Raw process address of the C_CSPlayerPawn object.
    // Stored for debugging / future reads; never dereferenced locally.
    uintptr_t pawnAddress = 0;

    // ── Vital state ──────────────────────────────────────────────────────────
    int      health    = 0;    // 0–100 (or higher with overheal)
    int      team      = 0;    // 2 = T, 3 = CT
    bool     alive     = false;

    // ── Position ─────────────────────────────────────────────────────────────
    Vec3     origin;           // world-space feet position

    // ── Identity ─────────────────────────────────────────────────────────────
    std::string name;          // UTF-8 player name (max 128 chars)

    // ── Convenience ──────────────────────────────────────────────────────────
    bool IsEnemy(int localTeam) const { return team != localTeam && team != 0; }
};

// ── EntityManager ─────────────────────────────────────────────────────────────

class EntityManager
{
public:
    /// Call once to bind the manager to the target process and module bases.
    void Init(HANDLE hProcess, uintptr_t clientBase);

    /// Read all player entities for the current frame.
    /// Call this once per tick before passing the results to the renderer.
    /// Returns false if the entity list cannot be reached (game not loaded).
    bool Update();

    /// The snapshot produced by the last successful Update().
    const std::vector<EntityData>& Entities() const { return m_entities; }

    /// Team of the local player (2 or 3).  0 if not yet read.
    int LocalTeam() const { return m_localTeam; }

    /// Diagnostic line for the debug HUD (hex addresses + raw counts).
    const std::wstring& DebugLine() const { return m_debugLine; }

private:
    HANDLE    m_hProcess   = NULL;
    uintptr_t m_clientBase = 0;

    int m_localTeam = 0;
    std::vector<EntityData> m_entities;
    std::wstring m_debugLine;

    // Internal helpers --------------------------------------------------------

    /// Reads one CCSPlayerController at `controllerAddr`, resolves the pawn,
    /// and populates `out`.  Returns false if the entity should be skipped.
    bool ReadController(uintptr_t controllerAddr,
                        uintptr_t chunkArrayAddr,
                        EntityData& out) const;

    /// Resolve a CEntityInstance* from a chunk array and entity index.
    /// Validates the handle stored in the identity slot before returning.
    uintptr_t GetEntityByIndex(uintptr_t chunkArrayAddr, int index) const;
};
