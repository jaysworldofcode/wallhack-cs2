#pragma once
//
// memory.hpp — External process memory reader
//
// Wraps Win32 ReadProcessMemory so the rest of the codebase never
// calls the raw API directly.  All reads are typed; a failed read
// returns the value-initialised default (0 / false / null) and sets
// the optional `ok` flag to false so callers can decide how to react.
//
// EDUCATIONAL NOTE:
//   ReadProcessMemory is a standard Windows debugging API (used by
//   debuggers, profilers, and anti-cheat systems themselves).  All
//   memory addresses below are resolved at runtime from the live
//   process — no file patching occurs.
//

#include <Windows.h>
#include <TlHelp32.h>   // CreateToolhelp32Snapshot, Process32First/Next
#include <Psapi.h>      // EnumProcessModules, GetModuleInformation
#include <cstdint>
#include <string_view>
#include <optional>

namespace Memory {

// ── Process handle ──────────────────────────────────────────────────────────

/// Opens a handle to the first process whose executable name matches
/// `processName` (e.g. L"cs2.exe").  Returns NULL on failure.
HANDLE OpenProcessByName(std::wstring_view processName);

/// Closes a handle previously returned by OpenProcessByName.
void   CloseProcessHandle(HANDLE hProcess);

// ── Module base ─────────────────────────────────────────────────────────────

/// Returns the base address of a loaded module (e.g. L"client.dll") inside
/// `hProcess`, or 0 on failure.
uintptr_t GetModuleBase(HANDLE hProcess, std::wstring_view moduleName);

// ── Typed read helpers ───────────────────────────────────────────────────────

/// Read `sizeof(T)` bytes at `address` in the target process.
/// On failure returns a value-initialised T{} and (if provided) sets *ok=false.
template<typename T>
[[nodiscard]] T Read(HANDLE hProcess, uintptr_t address, bool* ok = nullptr)
{
    T value{};
    SIZE_T bytesRead = 0;

    bool success = ReadProcessMemory(
        hProcess,
        reinterpret_cast<LPCVOID>(address),
        &value,
        sizeof(T),
        &bytesRead
    ) && (bytesRead == sizeof(T));

    if (ok) *ok = success;
    return value;
}

/// Follow a pointer chain (base + offset[0] + offset[1] + ...).
/// Each step dereferences a uintptr_t, then adds the next offset.
/// Returns 0 if any intermediate read fails.
uintptr_t ResolvePointerChain(
    HANDLE hProcess,
    uintptr_t base,
    std::initializer_list<uintptr_t> offsets
);

} // namespace Memory
