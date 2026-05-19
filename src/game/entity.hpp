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
#include <array>
#include <Windows.h>
#include <sstream>
#include <iomanip>

#include "offsets.hpp"
#include "../math/vec3.hpp"
#include "../config/remote_offsets.hpp"

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

    // ── Skeleton ─────────────────────────────────────────────────────────────
    std::array<Vec3, Offsets::Bones::kCount> bones{};
    bool hasBones = false;

    // ── Identity ─────────────────────────────────────────────────────────────
    std::string name;          // UTF-8 player name (max 128 chars)

    // ── Weapon ───────────────────────────────────────────────────────────────
    uint16_t weaponId = 0;     // m_iItemDefinitionIndex; 0 = unknown

    // ── Convenience ──────────────────────────────────────────────────────────
    bool IsEnemy(int localTeam) const { return team != localTeam && team != 0; }

    // Returns the display name for the active weapon, e.g. "AK-47" or "AWP".
    // Returns an empty string if the weapon ID is unknown or not yet read.
    const wchar_t* WeaponName() const
    {
        switch (weaponId)
        {
        // ── Pistols ──────────────────────────────────────────────────────────
        case   1: return L"Desert Eagle";
        case   2: return L"Dual Berettas";
        case   3: return L"Five-SeveN";
        case   4: return L"Glock-18";
        case  32: return L"P2000";
        case  36: return L"P250";
        case  30: return L"Tec-9";
        case  57: return L"CZ75-Auto";
        case  51: return L"USP-S";
        case  61: return L"R8 Revolver";

        // ── SMGs ─────────────────────────────────────────────────────────────
        case  17: return L"MAC-10";
        case  33: return L"MP7";
        case  34: return L"MP9";
        case  23: return L"MP5-SD";
        case  24: return L"UMP-45";
        case  19: return L"P90";
        case  26: return L"PP-Bizon";

        // ── Rifles ───────────────────────────────────────────────────────────
        case   7: return L"AK-47";
        case  10: return L"FAMAS";
        case  13: return L"Galil AR";
        case  16: return L"M4A4";
        case  60: return L"M4A1-S";
        case   8: return L"AUG";
        case  39: return L"SG 553";

        // ── Snipers ──────────────────────────────────────────────────────────
        case   9: return L"AWP";
        case  40: return L"SSG 08";
        case  11: return L"G3SG1";
        case  38: return L"SCAR-20";

        // ── Heavy ────────────────────────────────────────────────────────────
        case  14: return L"M249";
        case  28: return L"Negev";
        case  25: return L"XM1014";
        case  29: return L"Sawed-Off";
        case  27: return L"MAG-7";
        case  35: return L"Nova";

        // ── Utility ──────────────────────────────────────────────────────────
        case  42: return L"Flashbang";
        case  43: return L"HE Grenade";
        case  44: return L"Smoke";
        case  45: return L"Molotov";
        case  46: return L"Decoy";
        case  47: return L"Incendiary";
        case  48: return L"C4";
        case  31: return L"Zeus";
        case  52: return L"Taser";

        // ── Knife (any variant) ───────────────────────────────────────────────
        case  41: case  49: case  59:
        case  500: case  505: case  506: case  507: case  508:
        case  509: case  512: case  514: case  515: case  516: case  517:
        case  518: case  519: case  520: case  521: case  522: case  523:
        case  524: case  525: case  526: case  527: return L"Knife";

        default:  return L"";
        }
    }
};

// ── EntityManager ─────────────────────────────────────────────────────────────

class EntityManager
{
public:
    /// Call once to bind the manager to the target process and module bases.
    /// `offsets` provides the 3 runtime-fetched Client offsets.
    void Init(HANDLE hProcess, uintptr_t clientBase, const RemoteOffsets& offsets);

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

    // Runtime-fetched offsets (overrides compile-time defaults).
    uintptr_t m_offEntityList  = Offsets::Client::dwEntityList;
    uintptr_t m_offLocalPlayer = Offsets::Client::dwLocalPlayer;

    int m_localTeam = 0;
    std::vector<EntityData> m_entities;
    std::wstring m_debugLine;

    // ── Controller cache ──────────────────────────────────────────────────────
    // Scanning 64 entity-list slots costs ~128 RPM calls. We do it once every
    // kCacheFrames frames (~2 Hz at 60 fps) and store the stable controller
    // addresses + player names (names never change mid-match).
    struct CachedController
    {
        uintptr_t addr = 0;
        std::string name;
    };

    std::vector<CachedController> m_controllerCache;
    uintptr_t m_chunk0Ptr     = 0; // chunk-0 pointer (indices 0-511), cached
    int       m_frameCounter  = 0;

    // Refresh the controller list every N frames.
    static constexpr int kCacheFrames = 30; // 2 Hz at 60 fps

    // Internal helpers --------------------------------------------------------

    /// Reads one CCSPlayerController at `controllerAddr`, resolves the pawn,
    /// and populates `out`.  Returns false if the entity should be skipped.
    /// If `nameOverride` is non-null it is used instead of re-reading the name.
    bool ReadController(uintptr_t   controllerAddr,
                        uintptr_t   chunkArrayAddr,
                        EntityData& out,
                        const std::string* nameOverride = nullptr) const;

    /// Resolve a CEntityInstance* from a chunk array and entity index.
    /// Validates the handle stored in the identity slot before returning.
    /// `chunk0Cache` may be non-zero to skip the chunk-ptr RPM for index < 512.
    uintptr_t GetEntityByIndex(uintptr_t chunkArrayAddr,
                               int       index,
                               uintptr_t chunk0Cache = 0) const;
};
