#include "entity.hpp"
#include "../memory/memory.hpp"

#include <algorithm>
#include <cstring>  // memset

// ── EntityManager::Init ───────────────────────────────────────────────────────

void EntityManager::Init(HANDLE hProcess, uintptr_t clientBase)
{
    m_hProcess   = hProcess;
    m_clientBase = clientBase;
    m_entities.reserve(64);
}

// ── EntityManager::GetEntityByIndex ───────────────────────────────────────────

uintptr_t EntityManager::GetEntityByIndex(uintptr_t chunkArrayAddr, int index) const
{
    if (index <= 0)
        return 0;

    const int chunkIdx = index / Offsets::EntityList::kChunkSize;
    const int slotIdx  = index % Offsets::EntityList::kChunkSize;

    uintptr_t chunkPtr = Memory::Read<uintptr_t>(
        m_hProcess,
        chunkArrayAddr + static_cast<uintptr_t>(chunkIdx) * sizeof(uintptr_t)
    );
    if (!chunkPtr)
        return 0;

    uintptr_t identityAddr = chunkPtr + slotIdx * Offsets::EntityIdentity::kStride;

    uint32_t handle = Memory::Read<uint32_t>(
        m_hProcess,
        identityAddr + Offsets::EntityIdentity::pHandle
    );
    if (handle == 0 || handle == 0xFFFFFFFF)
        return 0;

    // Stale / recycled slots keep a handle whose index does not match the slot.
    if (Offsets::HandleToIndex(handle) != index)
        return 0;

    return Memory::Read<uintptr_t>(
        m_hProcess,
        identityAddr + Offsets::EntityIdentity::pEntity
    );
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

    uintptr_t chunkArrayAddr = entitySystemAddr + Offsets::EntityList::pChunks;

    // ── 2. Resolve the local player's team ───────────────────────────────────

    uintptr_t localControllerAddr = Memory::Read<uintptr_t>(
        m_hProcess,
        m_clientBase + Offsets::Client::dwLocalPlayer
    );

    uintptr_t localPawnAddr = 0;

    if (localControllerAddr)
    {
        uint32_t localPawnHandle = Memory::Read<uint32_t>(
            m_hProcess,
            localControllerAddr + Offsets::Controller::m_hPlayerPawn
        );
        int localPawnIdx = Offsets::HandleToIndex(localPawnHandle);
        localPawnAddr = GetEntityByIndex(chunkArrayAddr, localPawnIdx);

        if (localPawnAddr)
        {
            m_localTeam = static_cast<int>(Memory::Read<uint8_t>(
                m_hProcess,
                localPawnAddr + Offsets::Pawn::m_iTeamNum
            ));
        }
    }

    uint8_t ctrlTeam = localControllerAddr
        ? Memory::Read<uint8_t>(m_hProcess, localControllerAddr + Offsets::Pawn::m_iTeamNum)
        : 0;

    // ── 3. Walk player controllers (indices 1..64) ───────────────────────────
    //
    // Each connected player owns a CCSPlayerController in the entity list.
    // Resolving pawns via m_hPlayerPawn is far more reliable than scanning
    // every chunk slot for team 2/3 objects (which picks up unrelated entities).
    //
    int nControllers = 0, nResolved = 0;
    for (int idx = 1; idx <= Offsets::EntityList::kMaxPlayerSlots; ++idx)
    {
        uintptr_t controllerAddr = GetEntityByIndex(chunkArrayAddr, idx);
        if (!controllerAddr)
            continue;

        uint32_t pawnHandle = Memory::Read<uint32_t>(
            m_hProcess,
            controllerAddr + Offsets::Controller::m_hPlayerPawn
        );
        if (pawnHandle == 0 || pawnHandle == 0xFFFFFFFF)
            continue;   // not a player controller

        ++nControllers;

        EntityData data;
        if (!ReadController(controllerAddr, chunkArrayAddr, data))
            continue;

        if (data.pawnAddress == localPawnAddr)
            continue;

        ++nResolved;
        m_entities.push_back(std::move(data));
    }

    wchar_t buf[192];
    swprintf_s(buf, L"cTm=%d ctrl=%d ok=%d ps=%d",
        (int)ctrlTeam,
        nControllers, nResolved, (int)m_entities.size());
    m_debugLine = buf;
    return true;
}

// ── EntityManager::ReadController ─────────────────────────────────────────────

bool EntityManager::ReadController(uintptr_t controllerAddr,
                                   uintptr_t chunkArrayAddr,
                                   EntityData& out) const
{
    uint32_t pawnHandle = Memory::Read<uint32_t>(
        m_hProcess,
        controllerAddr + Offsets::Controller::m_hPlayerPawn
    );
    if (pawnHandle == 0xFFFFFFFF || pawnHandle == 0)
        return false;

    int pawnIdx = Offsets::HandleToIndex(pawnHandle);
    uintptr_t pawnAddr = GetEntityByIndex(chunkArrayAddr, pawnIdx);
    if (!pawnAddr)
        return false;

    uintptr_t sceneNode = Memory::Read<uintptr_t>(
        m_hProcess, pawnAddr + Offsets::Pawn::m_pGameSceneNode);
    if (!sceneNode)
        return false;

    uint8_t lifeState = Memory::Read<uint8_t>(
        m_hProcess,
        pawnAddr + Offsets::Pawn::m_lifeState
    );

    out.pawnAddress = pawnAddr;
    out.alive       = (lifeState == 0);
    out.team        = static_cast<int>(
        Memory::Read<uint8_t>(m_hProcess, pawnAddr + Offsets::Pawn::m_iTeamNum));
    out.health      = Memory::Read<int32_t>(
        m_hProcess, pawnAddr + Offsets::Pawn::m_iHealth);
    out.origin      = Memory::Read<Vec3>(
        m_hProcess, sceneNode + Offsets::GameSceneNode::m_vecAbsOrigin);

    char nameBuf[128]{};
    ReadProcessMemory(
        m_hProcess,
        reinterpret_cast<LPCVOID>(controllerAddr + Offsets::Controller::m_iszPlayerName),
        nameBuf,
        sizeof(nameBuf) - 1,
        nullptr
    );
    out.name = nameBuf;

    return true;
}
