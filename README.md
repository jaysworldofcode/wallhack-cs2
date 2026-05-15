# CS2 External Overlay

An external overlay for Counter-Strike 2 that renders a transparent, always-on-top window over the game using Direct2D. Demonstrates Win32 process memory reading, DWM transparency, and world-to-screen projection.

> **For educational and research use only.**  
> Running this against an online CS2 session violates Valve's Subscriber Agreement and will result in a VAC ban. Use against a local/offline server only.

---

## Features

- **ESP box** — corner-style bounding box drawn around each enemy player
- **HP bar** — colour-coded health bar (green → red) beside each box
- **Name tag** — player display name above the box
- **Debug HUD** — live status indicator showing process attach state and visible entity count
- **In-game menu** — toggled with `INSERT`; shows current resolution and lets you cycle presets
- **60 Hz frame cap** — low CPU footprint via frame-budget sleep

---

## Requirements

| Requirement | Version |
|---|---|
| Windows | 10 or later (x64) |
| CMake | 3.20 or later |
| MSVC | Visual Studio 2019 or later (MSVC v142+) |
| Windows 10 SDK | 10.0.18362 or later |

No third-party libraries are needed. All dependencies (`d2d1`, `dwrite`, `dwmapi`, `d3d11`, `dxgi`, `psapi`) ship with the Windows SDK.

---

## Building

### 1. Clone the repository

```
git clone <your-repo-url>
cd cs2c
```

### 2. Configure with CMake

```
cmake -B build -G "Visual Studio 16 2019" -A x64
```

If you have Visual Studio 2022, use `"Visual Studio 17 2022"` instead.

### 3. Build

```
cmake --build build --config Release
```

The compiled binary is placed at:

```
build\Release\cs2_overlay.exe
```

---

## Running

1. Launch **Counter-Strike 2** and wait until you are fully loaded into a map.
2. Run `cs2_overlay.exe` **as Administrator** (required for `ReadProcessMemory` access to cs2.exe).
3. A transparent overlay window will appear on top of the game.

If the game is not running you will see an error dialog — start CS2 first, then launch the overlay.

---

## Controls

| Key | Action |
|---|---|
| `INSERT` | Toggle the settings menu on / off |
| `← Left Arrow` | Cycle to the previous resolution preset (menu must be open) |
| `→ Right Arrow` | Cycle to the next resolution preset (menu must be open) |
| Close window / `Alt+F4` | Exit the overlay |

### Resolution presets

The overlay must match your in-game resolution for world-to-screen projection to be accurate. Cycle through presets with the arrow keys while the menu is open:

`1024×768` · `1280×720` · `1280×960` · `1366×768` · `1440×900` · `1600×900` · **`1920×1080`** · `2560×1440` · `3840×2160`

The default is **1920×1080** (index 6). Your selection resets when you restart the overlay.

---

## Project Structure

```
cs2c/
├── src/
│   ├── main.cpp               # Entry point, main loop
│   ├── game/
│   │   ├── offsets.hpp        # All memory offsets (update after patches)
│   │   ├── entity.hpp/.cpp    # EntityManager — reads player data from memory
│   ├── memory/
│   │   ├── memory.hpp/.cpp    # ReadProcessMemory wrappers
│   ├── overlay/
│   │   ├── overlay.hpp/.cpp   # Win32 transparent window (DWM)
│   ├── renderer/
│   │   ├── renderer.hpp/.cpp  # Direct2D drawing (boxes, HP bars, text)
│   ├── math/
│       ├── math_utils.hpp/.cpp # World-to-screen projection, AABB helpers
│       ├── vec3.hpp            # 3-component vector
└── CMakeLists.txt
```

---

## Troubleshooting

**"cs2.exe not found" dialog**  
CS2 is not running. Start the game and get into a map, then launch the overlay.

**"client.dll is not loaded" dialog**  
The game is still loading. Wait until you are fully in a match before launching the overlay.

**Boxes appear but are offset from players**  
Your in-game resolution does not match the overlay resolution. Open the menu (`INSERT`) and cycle presets until they align.

**Debug HUD shows yellow dot / 0 entities**  
The overlay may be out of date. Check for a newer release.

**Overlay window is blank / black**  
Direct2D device creation failed. Ensure your GPU drivers are up to date and that the Windows 10 SDK is installed.
