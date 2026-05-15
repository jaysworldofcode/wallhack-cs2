// main.cpp — CS2 external overlay entry point
//
// ┌────────────────────────────────────────────────────────────────────────────┐
// │  EDUCATIONAL / RESEARCH USE ONLY                                          │
// │                                                                            │
// │  This project demonstrates:                                                │
// │   • Win32 process memory reading (ReadProcessMemory)                      │
// │   • Transparent overlay construction (DWM + Direct2D)                    │
// │   • View-projection matrix mathematics (world-to-screen)                  │
// │   • AABB bounding-box and HP-bar rendering                                │
// │                                                                            │
// │  Running this against an online CS2 session violates Valve's Subscriber   │
// │  Agreement and will trigger a VAC ban.  The project is intended to be     │
// │  studied against a local/offline server only.                             │
// └────────────────────────────────────────────────────────────────────────────┘
//
// ── Frame loop ────────────────────────────────────────────────────────────────
//
//   Init
//    │
//    ▼
//   Attach to cs2.exe  ──fail──► exit with error message
//    │
//    ▼
//   Find client.dll base
//    │
//    ▼
//   Create overlay window + Direct2D renderer
//    │
//    ▼
//  ┌─────────────────────────────────────────────────────────────────────────┐
//  │  Per-frame (target ≈ 60 Hz)                                             │
//  │                                                                          │
//  │  1. PumpMessages()           — handle WM_QUIT / WM_KEYDOWN              │
//  │  2. Read ViewMatrix          — fresh each frame                          │
//  │  3. EntityManager::Update()  — snapshot all live player entities        │
//  │  4. Renderer::BeginFrame()   — clear to transparent                     │
//  │  5. For each entity:                                                     │
//  │       a. GetScreenBox()      — project AABB to screen                   │
//  │       b. DrawEntity()        — corner box + HP bar + name               │
//  │  6. Renderer::EndFrame()     — submit draw calls                        │
//  └─────────────────────────────────────────────────────────────────────────┘
//    │
//   WM_QUIT / INSERT key
//    │
//    ▼
//   Shutdown + CloseHandle
//

#include <Windows.h>
#include <chrono>
#include <thread>
#include <cstdio>

#include "memory/memory.hpp"
#include "game/entity.hpp"
#include "overlay/overlay.hpp"
#include "renderer/renderer.hpp"
#include "math/math_utils.hpp"
#include "game/offsets.hpp"

// ── Configuration ─────────────────────────────────────────────────────────────

static constexpr float kTargetFPS     = 60.f;
static constexpr int   kFrameBudgetMs =
    static_cast<int>(1000.f / kTargetFPS);  // ~16 ms per frame

// ── Resolution presets ────────────────────────────────────────────────────────

struct Resolution { int w; int h; const wchar_t* label; };

static constexpr Resolution kResolutions[] = {
    { 1024,  768, L"1024x768"  },
    { 1280,  720, L"1280x720"  },
    { 1280,  960, L"1280x960"  },
    { 1366,  768, L"1366x768"  },
    { 1440,  900, L"1440x900"  },
    { 1600,  900, L"1600x900"  },
    { 1920, 1080, L"1920x1080" },
    { 2560, 1440, L"2560x1440" },
    { 3840, 2160, L"3840x2160" },
};
static constexpr int kResCount = static_cast<int>(
    sizeof(kResolutions) / sizeof(kResolutions[0]));

// Default to 1920x1080
static int gResIdx = 6;

// ── WinMain ───────────────────────────────────────────────────────────────────

int WINAPI WinMain(
    _In_     HINSTANCE /*hInstance*/,
    _In_opt_ HINSTANCE /*hPrevInstance*/,
    _In_     LPSTR     /*lpCmdLine*/,
    _In_     int       /*nCmdShow*/)
{
    // ── 1. Attach to CS2 ──────────────────────────────────────────────────────

    HANDLE hProcess = Memory::OpenProcessByName(L"cs2.exe");
    if (!hProcess)
    {
        MessageBoxW(nullptr,
            L"cs2.exe not found.\n\nLaunch Counter-Strike 2 before running the overlay.",
            L"Overlay — Process Not Found",
            MB_ICONERROR | MB_OK);
        return 1;
    }

    uintptr_t clientBase = Memory::GetModuleBase(hProcess, L"client.dll");
    if (!clientBase)
    {
        MessageBoxW(nullptr,
            L"client.dll is not loaded.\n\nWait for the game to fully load into a match.",
            L"Overlay — Module Not Found",
            MB_ICONERROR | MB_OK);
        Memory::CloseProcessHandle(hProcess);
        return 1;
    }

    // ── 2. Create overlay window ──────────────────────────────────────────────

    Overlay overlay;
    if (!overlay.Create(kResolutions[gResIdx].w, kResolutions[gResIdx].h, L"CS2 Wallhack"))
    {
        MessageBoxW(nullptr, L"Failed to create overlay window.",
                    L"Overlay — Window Error", MB_ICONERROR | MB_OK);
        Memory::CloseProcessHandle(hProcess);
        return 1;
    }

    // ── 3. Initialise renderer ────────────────────────────────────────────────

    Renderer renderer;
    if (!renderer.Init(overlay.Hwnd(),
                       kResolutions[gResIdx].w,
                       kResolutions[gResIdx].h))
    {
        MessageBoxW(nullptr, L"Failed to initialise Direct2D renderer.",
                    L"Overlay — D2D Error", MB_ICONERROR | MB_OK);
        overlay.Destroy();
        Memory::CloseProcessHandle(hProcess);
        return 1;
    }

    // ── 4. Initialise entity manager ──────────────────────────────────────────

    EntityManager entityMgr;
    entityMgr.Init(hProcess, clientBase);

    // ── 5. Main loop ──────────────────────────────────────────────────────────

    bool menuVisible = true;    // open on launch so the user sees the controls immediately
    bool prevInsert  = false;  // edge-detection for INSERT

    Features feat;              // all features on by default
    int  menuCursor = 0;        // 0-3 = features, 4 = resolution
    bool prevUp = false, prevDown = false, prevLeft = false, prevRight = false;

    while (true)
    {
        const auto frameStart = std::chrono::steady_clock::now();

        // ── INSERT key: toggle menu ───────────────────────────────────────────
        const bool curInsert = (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0;
        if (curInsert && !prevInsert)
            menuVisible = !menuVisible;
        prevInsert = curInsert;

        // ── Arrow-key menu navigation (only while menu is open) ──────────────
        if (menuVisible)
        {
            const bool curUp    = (GetAsyncKeyState(VK_UP)    & 0x8000) != 0;
            const bool curDown  = (GetAsyncKeyState(VK_DOWN)  & 0x8000) != 0;
            const bool curLeft  = (GetAsyncKeyState(VK_LEFT)  & 0x8000) != 0;
            const bool curRight = (GetAsyncKeyState(VK_RIGHT) & 0x8000) != 0;

            // UP / DOWN — move cursor through 5 rows (0-3 features, 4 resolution)
            if (curUp   && !prevUp)   menuCursor = (menuCursor + 4) % 5;
            if (curDown && !prevDown) menuCursor = (menuCursor + 1) % 5;

            // LEFT / RIGHT — toggle feature or cycle resolution
            if (curLeft && !prevLeft)
            {
                if      (menuCursor == 0) feat.showBox    = !feat.showBox;
                else if (menuCursor == 1) feat.showName   = !feat.showName;
                else if (menuCursor == 2) feat.showHealth = !feat.showHealth;
                else if (menuCursor == 3) feat.showWeapon = !feat.showWeapon;
                else { gResIdx = (gResIdx - 1 + kResCount) % kResCount;
                       overlay.Resize(kResolutions[gResIdx].w, kResolutions[gResIdx].h);
                       renderer.Resize(kResolutions[gResIdx].w, kResolutions[gResIdx].h); }
            }
            if (curRight && !prevRight)
            {
                if      (menuCursor == 0) feat.showBox    = !feat.showBox;
                else if (menuCursor == 1) feat.showName   = !feat.showName;
                else if (menuCursor == 2) feat.showHealth = !feat.showHealth;
                else if (menuCursor == 3) feat.showWeapon = !feat.showWeapon;
                else { gResIdx = (gResIdx + 1) % kResCount;
                       overlay.Resize(kResolutions[gResIdx].w, kResolutions[gResIdx].h);
                       renderer.Resize(kResolutions[gResIdx].w, kResolutions[gResIdx].h); }
            }

            prevUp    = curUp;   prevDown  = curDown;
            prevLeft  = curLeft; prevRight = curRight;
        }
        else
        {
            prevUp = prevDown = prevLeft = prevRight = false;
        }

        // Process Windows messages; exit loop on WM_QUIT.
        if (!overlay.PumpMessages())
            break;

        // ── 5a. Read the current view-projection matrix ───────────────────────
        //
        // The matrix must be read as a single atomic RPM call (16 floats = 64 B)
        // to avoid tearing between rows read at different instants.
        //
        ViewMatrix viewMatrix = Memory::Read<ViewMatrix>(
            hProcess,
            clientBase + Offsets::Client::dwViewMatrix
        );

        // ── 5b. Snapshot all entities ─────────────────────────────────────────

        bool gotEntities = entityMgr.Update();
        const auto& entities = entityMgr.Entities();

        // ── 5c. Render ────────────────────────────────────────────────────────

        if (!renderer.BeginFrame())
        {
            // Device lost and recreation failed; try again next tick.
            std::this_thread::sleep_for(std::chrono::milliseconds(kFrameBudgetMs));
            continue;
        }

        // Always draw the debug HUD.
        // We are always "attached" here — if OpenProcess or GetModuleBase had
        // failed we would have shown a MessageBox and returned before this loop.
        //   GREEN  dot + count > 0  → offsets correct, everything works
        //   YELLOW dot + count = 0  → attached, but dwEntityList offset wrong
        renderer.DrawDebugHUD(/*entityListOk=*/gotEntities,
                               static_cast<int>(entities.size()),
                               entityMgr.DebugLine());

        // Always-visible watermark.
        renderer.DrawWatermark();

        // Toggleable menu panel (INSERT key).
        renderer.DrawMenu(menuVisible, kResolutions[gResIdx].label, feat, menuCursor);

        for (const EntityData& entity : entities)
        {
            // Project the entity's AABB from world space to screen space.
            auto screenBox = GetScreenBox(
                entity.origin,
                viewMatrix,
                static_cast<float>(renderer.Width()),
                static_cast<float>(renderer.Height())
            );

            if (!screenBox || !screenBox->Valid())
                continue;   // entity is behind the camera or off-screen

            const bool isEnemy = entity.IsEnemy(entityMgr.LocalTeam());
            if (!isEnemy) continue;        // don't draw teammates
            if (!entity.alive) continue;   // hide box while dead
            renderer.DrawEntity(entity, *screenBox, isEnemy, viewMatrix, feat);
        }

        renderer.EndFrame();

        // ── 5d. Frame-rate cap ────────────────────────────────────────────────
        //
        // Sleep for the remaining frame budget.  This keeps CPU usage low and
        // avoids hammering the kernel with thousands of RPM calls per second.
        //
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - frameStart
        ).count();

        if (elapsed < kFrameBudgetMs)
            std::this_thread::sleep_for(
                std::chrono::milliseconds(kFrameBudgetMs - elapsed));
    }

    // ── 6. Shutdown ───────────────────────────────────────────────────────────

    renderer.Shutdown();
    overlay.Destroy();
    Memory::CloseProcessHandle(hProcess);

    return 0;
}
