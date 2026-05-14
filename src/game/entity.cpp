#include "entity.hpp"
#include "../memory/memory.hpp"

#include <array>
#include <cstring>  // memset

// ── EntityManager::Init ───────────────────────────────────────────────────────

void EntityManager::Init(HANDLE hProcess, uintptr_t clientBase)
{
    m_hProcess   = hProcess;
    m_clientBase = clientBase;
    m_entities.reserve(64);
}

// ── EntityManager::Update ─────────────────────────────────────────────────────

bool EntityManager::Update()
{
    m_entities.clear();

    // ── 1. Resolve the entity list pointer ────────────────────────────────────
    //
    //   client.dll + dwEntityList  →  CGameEntitySystem*
    //   CGameEntitySystem + 0x10   →  chunk pointer array (inline, NOT a ptr-to-ptr)
    //
    uintptr_t entitySystemAddr = Memory::Read<uintptr_t>(
        m_hProcess,
        m_clientBase + Offsets::Client::dwEntityList
    );
    if (!entitySystemAddr)
    {
        wchar_t buf[128];
        swprintf_s(buf, L"entitySys=0 | base=%llX off=%llX",
            (unsigned long long)m_clientBase,
            (unsigned long long)Offsets::Client::dwEntityList);
        m_debugLine = buf;
        return false;
    }

    // The chunk array is EMBEDDED at +0x10 inside CGameEntitySystem — do NOT
    // dereference it a second time.  Just add the offset.
    uintptr_t chunkArrayAddr = entitySystemAddr + Offsets::EntityList::pChunks;

    // ── 2. Resolve the local player's team ───────────────────────────────────

    uintptr_t localControllerAddr = Memory::Read<uintptr_t>(
        m_hProcess,
        m_clientBase + Offsets::Client::dwLocalPlayer
    );

    uint32_t localPawnHandle = 0;
    uintptr_t localPawnAddr  = 0;

    if (localControllerAddr)
    {
        // Read the pawn handle from the local controller.
        localPawnHandle = Memory::Read<uint32_t>(
            m_hProcess,
            localControllerAddr + Offsets::Controller::m_hPlayerPawn
        );
        int localPawnIdx = Offsets::HandleToIndex(localPawnHandle);

        // Resolve the pawn address from the entity list.
        int chunkIdx = localPawnIdx / Offsets::EntityList::kChunkSize;
        int slotIdx  = localPawnIdx % Offsets::EntityList::kChunkSize;

        uintptr_t chunkPtr = Memory::Read<uintptr_t>(
            m_hProcess,
            chunkArrayAddr + chunkIdx * sizeof(uintptr_t)
        );
        if (chunkPtr)
        {
            uintptr_t localPawnIdentity = chunkPtr + slotIdx * Offsets::EntityIdentity::kStride;
            localPawnAddr = Memory::Read<uintptr_t>(
                m_hProcess,
                localPawnIdentity + Offsets::EntityIdentity::pObject
            );
            if (localPawnAddr)
            {
                m_localTeam = static_cast<int>(Memory::Read<uint8_t>(
                    m_hProcess,
                    localPawnAddr + Offsets::Pawn::m_iTeamNum
                ));
            }
        }
    }

    // Also read team directly from controller (C_BaseEntity::m_iTeamNum inherited)
    uint8_t ctrlTeam = localControllerAddr
        ? Memory::Read<uint8_t>(m_hProcess, localControllerAddr + Offsets::Pawn::m_iTeamNum)
        : 0;

    // ── 3. Scan chunk 1 directly for player PAWNS ─────────────────────────────
    //
    // Player pawns in CS2 have entity indices 512-1023 (chunk 1).
    // Instead of finding controllers in chunk 0 and resolving handles,
    // we scan chunk 1 directly and validate each slot as a pawn by
    // checking team (2/3) and health (1-200) — much more reliable.
    //
    uintptr_t chunk1 = Memory::Read<uintptr_t>(
        m_hProcess, chunkArrayAddr + 1 * sizeof(uintptr_t));
    if (!chunk1)
    {
        wchar_t buf[128];
        swprintf_s(buf, L"chunk1=0 | arr=%llX", (unsigned long long)chunkArrayAddr);
        m_debugLine = buf;
        return false;
    }

    int rawFound = 0, nNoScene = 0, nDormant = 0;
    for (int slot = 0; slot < Offsets::EntityList::kChunkSize; ++slot)
    {
        uintptr_t identityAddr = chunk1 + slot * Offsets::EntityIdentity::kStride;
        uintptr_t pawnAddr = Memory::Read<uintptr_t>(
            m_hProcess, identityAddr + Offsets::EntityIdentity::pObject);
        if (!pawnAddr) continue;

        // Validate as a player pawn: must have team 2 (T) or 3 (CT)
        uint8_t team = Memory::Read<uint8_t>(m_hProcess, pawnAddr + Offsets::Pawn::m_iTeamNum);
        if (team != 2 && team != 3) continue;

        // Health sanity check (0-200)
        int32_t health = Memory::Read<int32_t>(m_hProcess, pawnAddr + Offsets::Pawn::m_iHealth);
        if (health < 0 || health > 200) continue;

        ++rawFound;

        uintptr_t sceneNode = Memory::Read<uintptr_t>(
            m_hProcess, pawnAddr + Offsets::Pawn::m_pGameSceneNode);
        if (!sceneNode) { ++nNoScene; continue; }

        bool dormant = Memory::Read<bool>(
            m_hProcess, sceneNode + Offsets::GameSceneNode::m_bDormant);
        if (dormant) { ++nDormant; continue; }

        EntityData data;
        data.pawnAddress = pawnAddr;
        data.alive  = (Memory::Read<uint8_t>(m_hProcess, pawnAddr + Offsets::Pawn::m_lifeState) == 0);
        data.team   = static_cast<int>(team);
        data.health = health;
        data.origin = Memory::Read<Vec3>(
            m_hProcess, sceneNode + Offsets::GameSceneNode::m_vecAbsOrigin);
        data.name   = "";
        m_entities.push_back(data);
    }

    wchar_t buf[192];
    swprintf_s(buf, L"cTm=%d | chunk1=%llX raw=%d ns=%d dr=%d ps=%d",
        (int)ctrlTeam,
        (unsigned long long)chunk1,
        rawFound, nNoScene, nDormant, (int)m_entities.size());
    m_debugLine = buf;
    return true;
}

// ── EntityManager::ReadController ─────────────────────────────────────────────

bool EntityManager::ReadController(uintptr_t controllerAddr,
                                   EntityData& out) const
{
    // ── a. Resolve pawn address via handle ────────────────────────────────────

    uint32_t pawnHandle = Memory::Read<uint32_t>(
        m_hProcess,
        controllerAddr + Offsets::Controller::m_hPlayerPawn
    );
    if (pawnHandle == 0xFFFFFFFF || pawnHandle == 0)
        return false;  // invalid handle (slot unused)

    int pawnIdx   = Offsets::HandleToIndex(pawnHandle);
    int chunkIdx  = pawnIdx / Offsets::EntityList::kChunkSize;
    int slotIdx   = pawnIdx % Offsets::EntityList::kChunkSize;

    // Re-read the chunk array root each call (inexpensive; avoids stale ptr).
    uintptr_t entitySystemAddr = Memory::Read<uintptr_t>(
        m_hProcess,
        m_clientBase + Offsets::Client::dwEntityList
    );
    // Chunk array is embedded at +0x10 — direct offset, no extra dereference.
    uintptr_t chunkArray = entitySystemAddr + Offsets::EntityList::pChunks;
    uintptr_t chunk = Memory::Read<uintptr_t>(
        m_hProcess,
        chunkArray + chunkIdx * sizeof(uintptr_t)
    );
    if (!chunk) return false;

    // Each slot is a CEntityIdentity struct (0x78 bytes). The pawn object
    // pointer is at offset 0x10 within the struct.
    uintptr_t pawnIdentity = chunk + slotIdx * Offsets::EntityIdentity::kStride;
    uintptr_t pawnAddr = Memory::Read<uintptr_t>(
        m_hProcess,
        pawnIdentity + Offsets::EntityIdentity::pObject
    );
    if (!pawnAddr) return false;

    // ── b. Dormancy check ─────────────────────────────────────────────────────
    // In CS2, m_bDormant is on CGameSceneNode, not on the pawn directly.
    uintptr_t sceneNode = Memory::Read<uintptr_t>(
        m_hProcess, pawnAddr + Offsets::Pawn::m_pGameSceneNode);
    if (!sceneNode) return false;

    bool dormant = Memory::Read<bool>(
        m_hProcess, sceneNode + Offsets::GameSceneNode::m_bDormant);
    if (dormant) return false;

    // ── c. Life state ─────────────────────────────────────────────────────────
    uint8_t lifeState = Memory::Read<uint8_t>(
        m_hProcess,
        pawnAddr + Offsets::Pawn::m_lifeState
    );
    // 0 = alive; anything else = dying / dead — skip.
    if (lifeState != 0) return false;

    // ── d. Read vital fields ──────────────────────────────────────────────────

    out.pawnAddress = pawnAddr;
    out.alive       = true;

    out.team = static_cast<int>(
        Memory::Read<uint8_t>(m_hProcess, pawnAddr + Offsets::Pawn::m_iTeamNum));

    out.health = Memory::Read<int32_t>(
        m_hProcess, pawnAddr + Offsets::Pawn::m_iHealth);

    out.origin = Memory::Read<Vec3>(
        m_hProcess, sceneNode + Offsets::GameSceneNode::m_vecAbsOrigin);

    // ── e. Player name (from controller, not pawn) ────────────────────────────
    char nameBuf[128]{};
    ReadProcessMemory(
        m_hProcess,
        reinterpret_cast<LPCVOID>(controllerAddr + Offsets::Controller::m_iszPlayerName),
        nameBuf,
        sizeof(nameBuf) - 1,  // always keep null terminator
        nullptr
    );
    out.name = nameBuf;

    return true;
}
