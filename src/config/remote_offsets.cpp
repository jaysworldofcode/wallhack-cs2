//
// remote_offsets.cpp — WinHTTP fetch of offsets.json from GitHub.
//
// Uses only the Windows SDK (winhttp.lib) — no external dependencies.
//

#include "remote_offsets.hpp"
#include "../game/offsets.hpp"   // compiled-in defaults

#include <Windows.h>
#include <winhttp.h>

#include <string>
#include <cstdio>

// ── Helpers ───────────────────────────────────────────────────────────────────

// Find `key` in `json`, then extract the next 0x… hex literal after it.
// Returns `fallback` if the key or value is absent.
static uintptr_t ParseHexValue(const std::string& json,
                               const char*        key,
                               uintptr_t          fallback)
{
    const auto keyPos = json.find(key);
    if (keyPos == std::string::npos)
        return fallback;

    const auto hexPos = json.find("0x", keyPos);
    if (hexPos == std::string::npos)
        return fallback;

    try {
        return static_cast<uintptr_t>(
            std::stoull(json.substr(hexPos), nullptr, 16));
    } catch (...) {
        return fallback;
    }
}

// ── FetchRemoteOffsets ────────────────────────────────────────────────────────

RemoteOffsets FetchRemoteOffsets()
{
    // Start with the compiled-in defaults so we always return valid values.
    RemoteOffsets result;
    result.dwEntityList  = Offsets::Client::dwEntityList;
    result.dwLocalPlayer = Offsets::Client::dwLocalPlayer;
    result.dwViewMatrix  = Offsets::Client::dwViewMatrix;
    result.fromRemote    = false;

    // ── Open WinHTTP session ──────────────────────────────────────────────────

    HINTERNET hSession = WinHttpOpen(
        L"cs2overlay/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );
    if (!hSession)
        return result;

    // 3-second timeouts — don't block the user too long at startup.
    static constexpr DWORD kTimeoutMs = 3000;
    WinHttpSetTimeouts(hSession, kTimeoutMs, kTimeoutMs, kTimeoutMs, kTimeoutMs);

    // ── Connect to raw.githubusercontent.com ─────────────────────────────────

    HINTERNET hConnect = WinHttpConnect(
        hSession,
        L"raw.githubusercontent.com",
        INTERNET_DEFAULT_HTTPS_PORT,
        0
    );
    if (!hConnect) { WinHttpCloseHandle(hSession); return result; }

    // ── Open GET request ──────────────────────────────────────────────────────

    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect,
        L"GET",
        L"/jaysworldofcode/wallhack-cs2/main/offsets.json",
        nullptr,                        // HTTP/1.1
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE             // HTTPS
    );
    if (!hRequest)
    {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return result;
    }

    // ── Send & receive ────────────────────────────────────────────────────────

    if (!WinHttpSendRequest(hRequest,
                            WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
        || !WinHttpReceiveResponse(hRequest, nullptr))
    {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return result;
    }

    // Verify HTTP 200.
    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest,
                        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX,
                        &statusCode, &statusSize,
                        WINHTTP_NO_HEADER_INDEX);
    if (statusCode != 200)
    {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return result;
    }

    // Read up to 4 KB (the file is < 200 bytes).
    std::string body;
    body.reserve(512);
    DWORD bytesAvail = 0;
    while (WinHttpQueryDataAvailable(hRequest, &bytesAvail) && bytesAvail)
    {
        if (body.size() + bytesAvail > 4096)
            break;  // safety guard
        const size_t prevSize = body.size();
        body.resize(prevSize + bytesAvail);
        DWORD bytesRead = 0;
        WinHttpReadData(hRequest,
                        body.data() + prevSize,
                        bytesAvail,
                        &bytesRead);
        body.resize(prevSize + bytesRead);
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    if (body.empty())
        return result;

    // ── Parse JSON ────────────────────────────────────────────────────────────
    //
    // Expected format (order doesn't matter):
    //   {
    //     "dwEntityList":  "0x250C5B0",
    //     "dwLocalPlayer": "0x2345D50",
    //     "dwViewMatrix":  "0x236C2F0"
    //   }

    const uintptr_t el  = ParseHexValue(body, "dwEntityList",  result.dwEntityList);
    const uintptr_t lp  = ParseHexValue(body, "dwLocalPlayer", result.dwLocalPlayer);
    const uintptr_t vm  = ParseHexValue(body, "dwViewMatrix",  result.dwViewMatrix);

    // Only accept the remote values if all three parsed successfully.
    if (el && lp && vm)
    {
        result.dwEntityList  = el;
        result.dwLocalPlayer = lp;
        result.dwViewMatrix  = vm;
        result.fromRemote    = true;
    }

    return result;
}
