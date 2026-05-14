#include "entity.hpp"
#include "../memory/memory.hpp"

#include <algorithm>
#include <cstring>

// ── EntityManager::Init ───────────────────────────────────────────────────────

void EntityManager::Init(HANDLE hProcess, uintptr_t clientBase)
{
    m_hProcess   = hProcess;
    m_clientBase = clientBase;
    m_entities.reserve(64);
    m_controllerCache.reserve(64);
    // Force a full cache refresh on the very first Update().
    m_frameCounter = kCacheFrames;
}

// ── EntityManager::GetEntityByIndex ───────────────────────────────────────────
// chunk0Cache: if non-zero and index < 512 we skip the chunk-ptr RPM call.

uintptr_t EntityManager::GetEntityByIndex(uintptr_t chunkArrayAddr,
                                          int       index,
                                          uintptr_t chunk0Cache) const
{
    if (index <= 0)
        return 0;

    const int chunkIdx = index / Offsets::EntityList::kChunkSize;
    const int slotIdx  = index % Offsets::EntityList::kChunkSize;

    uintptr_t chunkPtr;
    if (chunkIdx == 0 && chunk0Cache)
    {
        chunkPtr = chunk0Cache;
    }
    else
    {
        chunkPtr = Memory::Read<uintptr_t>(
            m_hProcess,
            chunkArrayAddr + static_cast<uintptr_t>(chunkIdx) * sizeof(uintptr_t)
        );
    }
    if (!chunkPtr)
        return 0;

    uintptr_t identityAddr = chunkPtr + slotIdx * Offsets::EntityIdentity::kStride;

    // Read the identity struct (entity ptr + handle) in one call.
    struct Identity { uintptr_t entity; uint8_t pad[8]; uint32_t handle; };
    Identity id{};
    SIZE_T bytesRead = 0;
    if (!ReadProcessMemory(m_hProcess,
                           reinterpret_cast<LPCVOID>(identityAddr),
                           &id, sizeof(id), &bytesRead)
        || bytesRead < sizeof(id))
        return 0;

    if (id.handle == 0 || id.handle == 0xFFFFFFFF)
        return 0;

    if (Offsets::HandleToIndex(id.handle) != index)
        return 0;

    return id.entity;
}

// ── EntityManager::Update ─────────────────────────────────────────────────────

bool EntityManager::Update()
{
    m_entities.clear();
    ++m_frameCounter;

    // ── 1. Resolve entity list root (1 RPM) ───────────────────────────────────

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

    // Refresh the cached chunk-0 pointer whenever we do a full scan
    // (or on first call — m_chunk0Ptr starts at 0).
    if (!m_chunk0Ptr || m_frameCounter >= kCacheFrames)
    {
        m_chunk0Ptr = Memory::Read<uintptr_t>(
            m_hProcess,
            chunkArrayAddr  // chunk index 0 → offset 0
        );
    }

    // ── 2. Local player team (3 RPM → 2 RPM via chunk cache) ─────────────────

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
        localPawnAddr = GetEntityByIndex(chunkArrayAddr, localPawnIdx, m_chunk0Ptr);

        if (localPawnAddr)
        {
            m_localTeam = static_cast<int>(Memory::Read<uint8_t>(
                m_hProcess,
                localPawnAddr + Offsets::Pawn::m_iTeamNum
            ));
        }
    }

    // ── 3. Controller cache refresh (only every kCacheFrames) ─────────────────
    //
    // Scanning 64 slots costs ~128 RPM calls. We limit this to ~2 Hz.
    // Player names are captured here — they never change mid-match.
    //
    if (m_frameCounter >= kCacheFrames)
    {
        m_frameCounter = 0;
        m_controllerCache.clear();

        for (int idx = 1; idx <= Offsets::EntityList::kMaxPlayerSlots; ++idx)
        {
            uintptr_t addr = GetEntityByIndex(chunkArrayAddr, idx, m_chunk0Ptr);
            if (!addr)
                continue;

            // Confirm this is a player controller by checking the pawn handle.
            uint32_t pawnHandle = Memory::Read<uint32_t>(
                m_hProcess,
                addr + Offsets::Controller::m_hPlayerPawn
            );
            if (pawnHandle == 0 || pawnHandle == 0xFFFFFFFF)
                continue;

            CachedController cc;
            cc.addr = addr;

            // Read player name once here — saves 128-byte RPM every frame.
            char nameBuf[128]{};
            ReadProcessMemory(
                m_hProcess,
                reinterpret_cast<LPCVOID>(addr + Offsets::Controller::m_iszPlayerName),
                nameBuf, sizeof(nameBuf) - 1, nullptr
            );
            cc.name = nameBuf;

            m_controllerCache.push_back(std::move(cc));
        }
    }

    // ── 4. Per-frame entity update (cached controllers only) ──────────────────

    int nResolved = 0;
    for (const CachedController& cc : m_controllerCache)
    {
        EntityData data;
        if (!ReadController(cc.addr, chunkArrayAddr, data, &cc.name))
            continue;

        if (data.pawnAddress == localPawnAddr)
            continue;

        ++nResolved;
        m_entities.push_back(std::move(data));
    }

    wchar_t buf[192];
    swprintf_s(buf, L"cTm=%d cached=%d ps=%d",
        m_localTeam,
        static_cast<int>(m_controllerCache.size()),
        static_cast<int>(m_entities.size()));
    m_debugLine = buf;
    return true;
}

// ── EntityManager::ReadController ─────────────────────────────────────────────
//
// All pawn fields are read in ONE batched RPM call (188 bytes) instead of
// 4 separate calls, cutting syscall overhead by ~75% per entity per frame.

bool EntityManager::ReadController(uintptr_t          controllerAddr,
                                   uintptr_t          chunkArrayAddr,
                                   EntityData&        out,
                                   const std::string* nameOverride) const
{
    uint32_t pawnHandle = Memory::Read<uint32_t>(
        m_hProcess,
        controllerAddr + Offsets::Controller::m_hPlayerPawn
    );
    if (pawnHandle == 0xFFFFFFFF || pawnHandle == 0)
        return false;

    int pawnIdx = Offsets::HandleToIndex(pawnHandle);
    uintptr_t pawnAddr = GetEntityByIndex(chunkArrayAddr, pawnIdx, m_chunk0Ptr);
    if (!pawnAddr)
        return false;

    // ── Batch read: one RPM call for all pawn fields (188 bytes) ─────────────
    //
    //  Buffer layout  (relative to kBatchBase = m_pGameSceneNode = 0x330):
    //    [0x000]  uintptr_t  m_pGameSceneNode
    //    [0x01C]  int32_t    m_iHealth
    //    [0x024]  uint8_t    m_lifeState
    //    [0x0BB]  uint8_t    m_iTeamNum
    //
    uint8_t buf[Offsets::Pawn::kBatchSize]{};
    SIZE_T  bytesRead = 0;
    if (!ReadProcessMemory(
            m_hProcess,
            reinterpret_cast<LPCVOID>(pawnAddr + Offsets::Pawn::kBatchBase),
            buf,
            Offsets::Pawn::kBatchSize,
            &bytesRead)
        || bytesRead < Offsets::Pawn::kBatchSize)
        return false;

    uintptr_t sceneNode;
    std::memcpy(&sceneNode, buf + Offsets::Pawn::kOff_SceneNode, sizeof(sceneNode));
    if (!sceneNode)
        return false;

    int32_t  health;
    std::memcpy(&health, buf + Offsets::Pawn::kOff_Health, sizeof(health));

    const uint8_t lifeState = buf[Offsets::Pawn::kOff_LifeState];
    const uint8_t teamNum   = buf[Offsets::Pawn::kOff_TeamNum];

    // ── Scene node: origin only (1 RPM) ──────────────────────────────────────
    Vec3 origin = Memory::Read<Vec3>(
        m_hProcess,
        sceneNode + Offsets::GameSceneNode::m_vecAbsOrigin
    );

    out.pawnAddress = pawnAddr;
    out.alive       = (lifeState == 0);
    out.team        = static_cast<int>(teamNum);
    out.health      = health;
    out.origin      = origin;
    out.name        = nameOverride ? *nameOverride : "";

    return true;
}
