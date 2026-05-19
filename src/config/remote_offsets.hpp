#pragma once
//
// remote_offsets.hpp — Fetch the 3 runtime offsets from GitHub at startup.
//
// The file  offsets.json  lives at the repo root and is served at:
//   https://raw.githubusercontent.com/jaysworldofcode/wallhack-cs2/main/offsets.json
//
// If the fetch succeeds the exe uses those values; if it fails (no internet,
// GitHub down, parse error) it silently falls back to the values compiled
// into offsets.hpp — so the overlay always starts.
//

#include <cstdint>

struct RemoteOffsets
{
    uintptr_t dwEntityList  = 0;
    uintptr_t dwLocalPlayer = 0;
    uintptr_t dwViewMatrix  = 0;

    // true  → values came from GitHub
    // false → values are the compiled-in defaults from offsets.hpp
    bool fromRemote = false;
};

// Blocking HTTP call — call once before the main loop (completes in < 3 s).
RemoteOffsets FetchRemoteOffsets();
