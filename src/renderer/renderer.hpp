#pragma once
//
// renderer.hpp — Direct2D ESP renderer
//
// Owns all Direct2D resources (factory, render target, brushes).
// Exposes a simple Begin / DrawEntity* / End API so the main loop
// only needs to call three functions per frame.
//
// ┌─────── HP Bar layout ────────────────────────────────────────────────┐
// │  [████████████░░░░] ← gradient fill (green→red) on the left side    │
// │  └─ 4 px wide bar, height matches the AABB, 2 px gap from the box   │
// └─────────────────────────────────────────────────────────────────────┘
//
// ┌─────── AABB Box layout ──────────────────────────────────────────────┐
// │  Thin 1-px stroke rectangle.                                        │
// │  Enemies: red (#FF4444)    Teammates: green (#44FF44)               │
// └─────────────────────────────────────────────────────────────────────┘
//

#include <Windows.h>
#include <d2d1.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <wrl/client.h>   // Microsoft::WRL::ComPtr (smart ptr for COM objects)

#include "../game/entity.hpp"
#include "../math/math_utils.hpp"

using Microsoft::WRL::ComPtr;

class Renderer
{
public:
    /// Initialize Direct2D and bind to an existing HWND.
    bool Init(HWND hwnd, int width, int height);

    /// Release all Direct2D resources.
    void Shutdown();

    /// Begin a new frame: create the render target if lost, BeginDraw, clear.
    /// Returns false if the device is lost and recreation failed.
    bool BeginFrame();

    /// Draw one entity's bounding box and HP bar.
    ///
    /// @param entity    Snapshot from EntityManager.
    /// @param box       Pre-computed screen AABB from GetScreenBox().
    /// @param isEnemy   Selects colour scheme (red vs. green).
    void DrawEntity(const EntityData& entity,
                    const ScreenBox&  box,
                    bool              isEnemy);

    /// Submit all draw calls to the GPU.
    void EndFrame();

    /// Draw a status bar in the top-left corner showing connection state and
    /// how many entities were found this frame.  Always visible regardless of
    /// offsets — use this to confirm D2D is working and to diagnose issues.
    ///
    ///   [GREEN dot]  Attached — N entities        ← offsets OK
    ///   [GREEN dot]  Attached — 0 entities        ← entity-list offset wrong
    ///   [RED   dot]  Not attached                 ← process / module missing
    void DrawDebugHUD(bool attached, int entityCount, const std::wstring& diagLine = {});

    // Dimensions (needed by the caller to pass to WorldToScreen / GetScreenBox).
    int Width()  const { return m_width;  }
    int Height() const { return m_height; }

private:
    // ── Configuration constants ───────────────────────────────────────────────
    static constexpr float kBoxStroke      = 1.2f;  // AABB outline width (px)
    static constexpr float kHpBarWidth     = 5.0f;  // HP bar width (px)
    static constexpr float kHpBarGap       = 3.0f;  // gap between box and HP bar
    static constexpr float kHpBarStroke    = 0.8f;  // HP bar border stroke width
    static constexpr float kNameOffsetY    = 3.0f;  // name label Y gap above box

    // ── Rendering state ───────────────────────────────────────────────────────
    HWND  m_hwnd   = nullptr;
    int   m_width  = 0;
    int   m_height = 0;

    // Direct2D
    ComPtr<ID2D1Factory>          m_d2dFactory;
    ComPtr<ID2D1HwndRenderTarget> m_renderTarget;

    // DirectWrite (text)
    ComPtr<IDWriteFactory>        m_dwFactory;
    ComPtr<IDWriteTextFormat>     m_textFormat;

    // Shared brushes (created once; colour modified per draw call).
    ComPtr<ID2D1SolidColorBrush>  m_brush;         // general-purpose
    ComPtr<ID2D1SolidColorBrush>  m_hpFillBrush;   // HP bar fill
    ComPtr<ID2D1SolidColorBrush>  m_hpBgBrush;     // HP bar empty background
    ComPtr<ID2D1SolidColorBrush>  m_shadowBrush;   // text drop shadow

    // ── Internal helpers ──────────────────────────────────────────────────────

    /// (Re)create the render target after a D2DERR_RECREATE_TARGET error.
    bool CreateDeviceResources();

    /// Release resources that must be recreated with the render target.
    void ReleaseDeviceResources();

    /// Draw a 1-pixel corner-bracket outline (less visually noisy than a full
    /// rectangle at large distances).
    void DrawCornerBox(const D2D1_RECT_F& rect, ID2D1Brush* brush);

    /// Draw the HP bar to the left of the AABB.
    void DrawHpBar(const ScreenBox& box, int health);

    /// Draw the player's name above the box.
    void DrawName(const EntityData& entity, const ScreenBox& box);

    /// Compute a D2D colour for a health value (green at 100, red at 0).
    static D2D1_COLOR_F HpColour(int health);
};
