#include "memory.hpp"

#include <vector>
#include <string>

namespace Memory {

// ── OpenProcessByName ────────────────────────────────────────────────────────

HANDLE OpenProcessByName(std::wstring_view processName)
{
    // Take a snapshot of all running processes.
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE)
        return NULL;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);

    HANDLE hProcess = NULL;

    if (Process32FirstW(hSnap, &entry))
    {
        do {
            // Case-insensitive compare of the executable name.
            if (_wcsicmp(entry.szExeFile, processName.data()) == 0)
            {
                // PROCESS_VM_READ  — required for ReadProcessMemory
                // PROCESS_QUERY_INFORMATION — required for EnumProcessModules
                hProcess = OpenProcess(
                    PROCESS_VM_READ | PROCESS_QUERY_INFORMATION,
                    FALSE,
                    entry.th32ProcessID
                );
                break;
            }
        } while (Process32NextW(hSnap, &entry));
    }

    CloseHandle(hSnap);
    return hProcess; // NULL if not found / access denied
}

// ── CloseProcessHandle ───────────────────────────────────────────────────────

void CloseProcessHandle(HANDLE hProcess)
{
    if (hProcess && hProcess != INVALID_HANDLE_VALUE)
        CloseHandle(hProcess);
}

// ── GetModuleBase ────────────────────────────────────────────────────────────

uintptr_t GetModuleBase(HANDLE hProcess, std::wstring_view moduleName)
{
    // Enumerate all modules loaded in the target process.
    HMODULE modules[1024]{};
    DWORD   needed = 0;

    if (!EnumProcessModulesEx(hProcess, modules, sizeof(modules), &needed,
                               LIST_MODULES_64BIT))
    {
        return 0;
    }

    const DWORD moduleCount = needed / sizeof(HMODULE);

    for (DWORD i = 0; i < moduleCount; ++i)
    {
        wchar_t name[MAX_PATH]{};
        if (!GetModuleFileNameExW(hProcess, modules[i], name, MAX_PATH))
            continue;

        // GetModuleFileNameEx returns the full path; we compare only the
        // filename component (everything after the last backslash).
        const wchar_t* filename = wcsrchr(name, L'\\');
        filename = filename ? filename + 1 : name;

        if (_wcsicmp(filename, moduleName.data()) == 0)
            return reinterpret_cast<uintptr_t>(modules[i]);
    }

    return 0; // module not found
}

// ── ResolvePointerChain ──────────────────────────────────────────────────────

uintptr_t ResolvePointerChain(
    HANDLE hProcess,
    uintptr_t base,
    std::initializer_list<uintptr_t> offsets)
{
    uintptr_t address = base;

    for (uintptr_t offset : offsets)
    {
        bool ok = false;
        address = Read<uintptr_t>(hProcess, address, &ok);
        if (!ok || address == 0)
            return 0;
        address += offset;
    }

    return address;
}

} // namespace Memory
