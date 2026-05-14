#include "renderer.hpp"

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "dwmapi.lib")

#include <cmath>
#include <algorithm>
#include <string>

// ── Renderer::Init ────────────────────────────────────────────────────────────

bool Renderer::Init(HWND hwnd, int width, int height)
{
    m_hwnd   = hwnd;
    m_width  = width;
    m_height = height;

    // Create the device-independent D2D1 factory.
    HRESULT hr = D2D1CreateFactory(
        D2D1_FACTORY_TYPE_SINGLE_THREADED,
        m_d2dFactory.GetAddressOf()
    );
    if (FAILED(hr)) return false;

    // Create the DirectWrite factory for text rendering.
    hr = DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(m_dwFactory.GetAddressOf())
    );
    if (FAILED(hr)) return false;

    // Create a small monospaced text format for player names / labels.
    hr = m_dwFactory->CreateTextFormat(
        L"Consolas",
        nullptr,
        DWRITE_FONT_WEIGHT_REGULAR,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        11.f,          // font size in DIP (device-independent pixels)
        L"en-us",
        m_textFormat.GetAddressOf()
    );
    if (FAILED(hr)) return false;

    m_textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    m_textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

    return CreateDeviceResources();
}

// ── Renderer::CreateDeviceResources ──────────────────────────────────────────

bool Renderer::CreateDeviceResources()
{
    // Create a hardware-accelerated HwndRenderTarget.
    D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED),
        0.f, 0.f
    );

    D2D1_HWND_RENDER_TARGET_PROPERTIES hwndProps = D2D1::HwndRenderTargetProperties(
        m_hwnd,
        D2D1::SizeU(m_width, m_height),
        D2D1_PRESENT_OPTIONS_NONE
    );

    HRESULT hr = m_d2dFactory->CreateHwndRenderTarget(
        rtProps, hwndProps, m_renderTarget.GetAddressOf()
    );
    if (FAILED(hr)) return false;

    // ── Create shared brushes ─────────────────────────────────────────────────

    hr = m_renderTarget->CreateSolidColorBrush(
        D2D1::ColorF(D2D1::ColorF::White), m_brush.GetAddressOf());
    if (FAILED(hr)) return false;

    // HP fill brush — colour is set per entity; initial colour doesn't matter.
    hr = m_renderTarget->CreateSolidColorBrush(
        D2D1::ColorF(0.f, 1.f, 0.f, 1.f), m_hpFillBrush.GetAddressOf());
    if (FAILED(hr)) return false;

    // Dark semi-transparent background for the empty portion of the HP bar.
    hr = m_renderTarget->CreateSolidColorBrush(
        D2D1::ColorF(0.f, 0.f, 0.f, 0.6f), m_hpBgBrush.GetAddressOf());
    if (FAILED(hr)) return false;

    // Drop-shadow brush for text.
    hr = m_renderTarget->CreateSolidColorBrush(
        D2D1::ColorF(0.f, 0.f, 0.f, 0.8f), m_shadowBrush.GetAddressOf());
    if (FAILED(hr)) return false;

    return true;
}

// ── Renderer::ReleaseDeviceResources ─────────────────────────────────────────

void Renderer::ReleaseDeviceResources()
{
    m_brush.Reset();
    m_hpFillBrush.Reset();
    m_hpBgBrush.Reset();
    m_shadowBrush.Reset();
    m_renderTarget.Reset();
}

// ── Renderer::Shutdown ────────────────────────────────────────────────────────

void Renderer::Shutdown()
{
    ReleaseDeviceResources();
    m_textFormat.Reset();
    m_dwFactory.Reset();
    m_d2dFactory.Reset();
}

// ── Renderer::BeginFrame ──────────────────────────────────────────────────────

bool Renderer::BeginFrame()
{
    if (!m_renderTarget)
    {
        if (!CreateDeviceResources())
            return false;
    }

    m_renderTarget->BeginDraw();

    // Clear to fully transparent black — the DWM composites this with
    // the desktop beneath, producing a transparent window background.
    m_renderTarget->Clear(D2D1::ColorF(0.f, 0.f, 0.f, 0.f));

    return true;
}

// ── Renderer::EndFrame ────────────────────────────────────────────────────────

void Renderer::EndFrame()
{
    HRESULT hr = m_renderTarget->EndDraw();

    // D2DERR_RECREATE_TARGET: device was lost (GPU driver crash, fullscreen
    // switch, etc.).  Release resources and recreate on the next frame.
    if (hr == D2DERR_RECREATE_TARGET)
    {
        ReleaseDeviceResources();
        CreateDeviceResources();  // best-effort; will retry next frame if fails
    }
}

// ── Renderer::DrawEntity ──────────────────────────────────────────────────────

void Renderer::DrawEntity(const EntityData& entity,
                          const ScreenBox&  box,
                          bool              isEnemy)
{
    // ── Colour scheme ─────────────────────────────────────────────────────────
    // Red for enemies, green for teammates.  Full opacity.
    D2D1_COLOR_F boxColour = isEnemy
        ? D2D1::ColorF(1.0f, 0.27f, 0.27f, 1.0f)   // #FF4545
        : D2D1::ColorF(0.27f, 1.0f, 0.27f, 1.0f);  // #45FF45

    m_brush->SetColor(boxColour);

    D2D1_RECT_F rect = D2D1::RectF(box.left, box.top, box.right, box.bottom);

    // ── Draw the AABB bounding box ────────────────────────────────────────────
    DrawCornerBox(rect, m_brush.Get());

    // ── Draw the HP bar ───────────────────────────────────────────────────────
    DrawHpBar(box, entity.health);

    // ── Draw the player name ──────────────────────────────────────────────────
    DrawName(entity, box);
}

// ── Renderer::DrawCornerBox ───────────────────────────────────────────────────
//
// Corner brackets are a common ESP aesthetic: four L-shaped strokes at the
// corners instead of a full rectangle outline, reducing visual clutter at
// range.  The bracket arm length is 1/5 of the box dimension.

void Renderer::DrawCornerBox(const D2D1_RECT_F& r, ID2D1Brush* brush)
{
    const float armX = (r.right  - r.left) * 0.20f;
    const float armY = (r.bottom - r.top)  * 0.20f;

    auto Line = [&](float x0, float y0, float x1, float y1) {
        m_renderTarget->DrawLine(
            D2D1::Point2F(x0, y0),
            D2D1::Point2F(x1, y1),
            brush, kBoxStroke
        );
    };

    // Top-left
    Line(r.left,         r.top,          r.left + armX,  r.top);
    Line(r.left,         r.top,          r.left,          r.top + armY);

    // Top-right
    Line(r.right - armX, r.top,          r.right,         r.top);
    Line(r.right,        r.top,          r.right,         r.top + armY);

    // Bottom-left
    Line(r.left,         r.bottom - armY, r.left,          r.bottom);
    Line(r.left,         r.bottom,        r.left + armX,   r.bottom);

    // Bottom-right
    Line(r.right,        r.bottom - armY, r.right,         r.bottom);
    Line(r.right - armX, r.bottom,        r.right,         r.bottom);
}

// ── Renderer::DrawHpBar ───────────────────────────────────────────────────────
//
// Layout (left side of the AABB):
//
//   [ dark bg ] — full bar height (box.top → box.bottom)
//   [ coloured fill ] — scaled by (health / 100), grows upward from bottom
//
// The fill direction is bottom-to-top so that as HP decreases, the bar
// shrinks from the top — matching the mental model of a depleting resource.

void Renderer::DrawHpBar(const ScreenBox& box, int health)
{
    const float clampedHp  = static_cast<float>(std::clamp(health, 0, 100));
    const float fillFrac   = clampedHp / 100.f;

    const float barRight   = box.left  - kHpBarGap;
    const float barLeft    = barRight  - kHpBarWidth;
    const float barTop     = box.top;
    const float barBottom  = box.bottom;
    const float barHeight  = barBottom - barTop;

    // ── Background (full height, dark) ────────────────────────────────────────
    D2D1_RECT_F bgRect = D2D1::RectF(barLeft, barTop, barRight, barBottom);
    m_renderTarget->FillRectangle(bgRect, m_hpBgBrush.Get());

    // ── Filled portion (bottom-aligned, grows upward) ─────────────────────────
    D2D1_RECT_F fillRect = D2D1::RectF(
        barLeft,
        barBottom - (barHeight * fillFrac),  // top of the fill region
        barRight,
        barBottom
    );

    m_hpFillBrush->SetColor(HpColour(health));
    m_renderTarget->FillRectangle(fillRect, m_hpFillBrush.Get());

    // ── Thin border around the whole bar ─────────────────────────────────────
    m_shadowBrush->SetOpacity(0.9f);
    m_renderTarget->DrawRectangle(bgRect, m_shadowBrush.Get(), kHpBarStroke);
}

// ── Renderer::DrawName ────────────────────────────────────────────────────────

void Renderer::DrawName(const EntityData& entity, const ScreenBox& box)
{
    if (entity.name.empty()) return;

    // Convert UTF-8 name to wide string for DirectWrite.
    const int wlen = MultiByteToWideChar(CP_UTF8, 0,
                                         entity.name.c_str(), -1,
                                         nullptr, 0);
    if (wlen <= 0) return;

    std::wstring wname(static_cast<size_t>(wlen - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, entity.name.c_str(), -1, wname.data(), wlen);

    // Label rect: horizontally centred above the box.
    D2D1_RECT_F labelRect = D2D1::RectF(
        box.left,
        box.top - 14.f - kNameOffsetY,  // 14 ≈ one line of 11pt text
        box.right,
        box.top - kNameOffsetY
    );

    // Drop shadow (1 px offset).
    m_shadowBrush->SetOpacity(0.85f);
    D2D1_RECT_F shadowRect = D2D1::RectF(
        labelRect.left + 1.f, labelRect.top + 1.f,
        labelRect.right + 1.f, labelRect.bottom + 1.f
    );
    m_renderTarget->DrawText(
        wname.c_str(), static_cast<UINT32>(wname.size()),
        m_textFormat.Get(), shadowRect, m_shadowBrush.Get()
    );

    // Main label in white.
    m_brush->SetColor(D2D1::ColorF(1.f, 1.f, 1.f, 1.f));
    m_renderTarget->DrawText(
        wname.c_str(), static_cast<UINT32>(wname.size()),
        m_textFormat.Get(), labelRect, m_brush.Get()
    );
}

// ── Renderer::HpColour ───────────────────────────────────────────────────────
//
// Interpolates from green (100 HP) → yellow (50 HP) → red (0 HP).
// Uses two linear segments so the transition is perceptually smooth.

D2D1_COLOR_F Renderer::HpColour(int health)
{
    const float t = std::clamp(static_cast<float>(health) / 100.f, 0.f, 1.f);

    float r, g;
    if (t >= 0.5f)
    {
        // 100→50 HP: green to yellow (R rises 0→1, G stays at 1).
        const float seg = (t - 0.5f) * 2.f;  // [0,1] within this segment
        r = 1.f - seg;
        g = 1.f;
    }
    else
    {
        // 50→0 HP: yellow to red (G falls 1→0, R stays at 1).
        const float seg = t * 2.f;            // [0,1] within this segment
        r = 1.f;
        g = seg;
    }

    return D2D1::ColorF(r, g, 0.f, 1.f);
}

// ── Renderer::DrawDebugHUD ────────────────────────────────────────────────────
//
// Always-visible diagnostic bar in the top-left corner.
// Use it to confirm Direct2D is working and to narrow down offset problems.
//
//   Dot colour:  GREEN = process attached + client.dll found
//                RED   = process / module not found
//   Text:        "Attached | entities: N" or "Not attached"
//
// Diagnosis guide:
//   RED  dot                → cs2.exe not found or access denied (run as Admin)
//   GREEN, entities = 0     → dwEntityList offset is wrong
//   GREEN, entities > 0,
//     but no boxes visible  → dwViewMatrix wrong, or W2S projection mismatch
//                             (check kOverlayWidth/Height vs actual resolution)

void Renderer::DrawDebugHUD(bool attached, int entityCount, const std::wstring& diagLine)
{
    // NOTE: `attached` here means "entity list was readable", not "process found".
    // The process is always found at this point — we'd have exited before the
    // render loop otherwise.
    //
    //   GREEN  = entity list readable, N entities found
    //   YELLOW = process attached, but entity list offset (dwEntityList) is wrong
    //
    const float dotX = 14.f, dotY = 14.f, dotR = 5.f;
    D2D1_ELLIPSE dot = D2D1::Ellipse(D2D1::Point2F(dotX, dotY), dotR, dotR);

    D2D1_COLOR_F dotColour;
    std::wstring text;

    if (attached && entityCount > 0)
    {
        dotColour = D2D1::ColorF(0.f, 1.f, 0.2f, 1.f);   // green — working
        text = L"Attached  |  entities: " + std::to_wstring(entityCount);
    }
    else
    {
        dotColour = D2D1::ColorF(1.f, 0.85f, 0.f, 1.f);  // yellow
        text = diagLine.empty()
            ? L"Attached  |  dwEntityList offset wrong \u2014 update offsets.hpp"
            : diagLine;
    }

    m_brush->SetColor(dotColour);
    m_renderTarget->FillEllipse(dot, m_brush.Get());

    m_shadowBrush->SetOpacity(1.f);
    m_renderTarget->DrawEllipse(dot, m_shadowBrush.Get(), 1.f);

    // Shadow
    m_shadowBrush->SetOpacity(0.9f);
    m_renderTarget->DrawText(
        text.c_str(), static_cast<UINT32>(text.size()),
        m_textFormat.Get(),
        D2D1::RectF(dotX * 2.f + 1.f, dotY - 7.f + 1.f, 900.f, dotY + 10.f),
        m_shadowBrush.Get()
    );

    // Foreground
    m_brush->SetColor(D2D1::ColorF(1.f, 1.f, 1.f, 1.f));
    m_renderTarget->DrawText(
        text.c_str(), static_cast<UINT32>(text.size()),
        m_textFormat.Get(),
        D2D1::RectF(dotX * 2.f, dotY - 7.f, 900.f, dotY + 10.f),
        m_brush.Get()
    );
}
