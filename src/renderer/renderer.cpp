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
    m_textFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

    // Left-aligned format for the menu panel and watermark.
    hr = m_dwFactory->CreateTextFormat(
        L"Consolas",
        nullptr,
        DWRITE_FONT_WEIGHT_REGULAR,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        10.f,
        L"en-us",
        m_menuTextFormat.GetAddressOf()
    );
    if (FAILED(hr)) return false;

    m_menuTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    m_menuTextFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

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
        D2D1_PRESENT_OPTIONS_IMMEDIATELY
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

// ── Renderer::Resize ─────────────────────────────────────────────────────────

void Renderer::Resize(int width, int height)
{
    m_width  = width;
    m_height = height;
    ReleaseDeviceResources();
    CreateDeviceResources();
}

// ── Renderer::Shutdown ────────────────────────────────────────────────────────

void Renderer::Shutdown()
{
    ReleaseDeviceResources();
    m_textFormat.Reset();
    m_menuTextFormat.Reset();
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
                          bool              isEnemy,
                          const ViewMatrix& vm,
                          const Features&   feat)
{
    (void)isEnemy;
    (void)vm;

    m_brush->SetColor(D2D1::ColorF(0.27f, 1.0f, 0.27f, 1.0f));

    if (feat.showBox)
        DrawCornerBox(D2D1::RectF(box.left, box.top, box.right, box.bottom), m_brush.Get());

    if (feat.showWeapon)
        DrawWeaponName(entity, box);

    if (feat.showName)
        DrawName(entity, box);

    if (feat.showHealth)
        DrawHealthNumber(entity.health, box);
}

// ── Renderer::DrawCornerBox ───────────────────────────────────────────────────

void Renderer::DrawCornerBox(const D2D1_RECT_F& r, ID2D1Brush* brush)
{
    const float armX = (r.right  - r.left) * 0.22f;
    const float armY = (r.bottom - r.top)  * 0.22f;

    auto Line = [&](float x0, float y0, float x1, float y1) {
        m_renderTarget->DrawLine(
            D2D1::Point2F(x0, y0), D2D1::Point2F(x1, y1),
            brush, kBoxStroke);
    };

    // Top-left
    Line(r.left,         r.top,           r.left + armX,  r.top);
    Line(r.left,         r.top,           r.left,          r.top + armY);
    // Top-right
    Line(r.right - armX, r.top,           r.right,         r.top);
    Line(r.right,        r.top,           r.right,         r.top + armY);
    // Bottom-left
    Line(r.left,         r.bottom - armY, r.left,          r.bottom);
    Line(r.left,         r.bottom,        r.left + armX,   r.bottom);
    // Bottom-right
    Line(r.right,        r.bottom - armY, r.right,         r.bottom);
    Line(r.right - armX, r.bottom,        r.right,         r.bottom);
}

// ── Renderer::DrawSkeleton ────────────────────────────────────────────────────
//
// Projects each bone's world position to screen space and draws lines between
// connected joints.  Bones that project behind the camera are skipped for that
// limb segment — the rest of the skeleton still renders cleanly.
//
// Bone connections:
//   Head  → Neck  → Chest → Spine → Pelvis
//   Chest → L/R Shoulder → Elbow → Wrist
//   Pelvis → L/R Hip → Knee → Ankle

void Renderer::DrawSkeleton(const EntityData& entity, const ViewMatrix& vm)
{
    if (!entity.hasBones) return;

    const float sw = static_cast<float>(m_width);
    const float sh = static_cast<float>(m_height);

    // Project all bones into a screen-space array indexed by bone ID.
    // Entries that are behind the camera remain as std::nullopt.
    using OptPt = std::optional<D2D1_POINT_2F>;
    std::array<OptPt, Offsets::Bones::kCount> pt{};
    for (int i = 0; i < Offsets::Bones::kCount; ++i)
    {
        auto s = WorldToScreen(entity.bones[i], vm, sw, sh);
        if (s) pt[i] = D2D1::Point2F(s->x, s->y);
    }

    constexpr float kStroke     = 1.0f;
    constexpr float kJointR     = 1.5f;  // radius of joint dots
    constexpr float kHeadR      = 4.5f;  // radius of head circle

    // Green for bones, slightly dimmer shadow for readability.
    const D2D1_COLOR_F kBoneCol   = D2D1::ColorF(0.27f, 1.0f, 0.27f, 1.0f);
    const D2D1_COLOR_F kShadowCol = D2D1::ColorF(0.f,   0.f,  0.f,  0.6f);

    namespace OB = Offsets::Bones;

    // Draw a bone line with a 1-px dark shadow for contrast.
    auto Bone = [&](int a, int b)
    {
        if (!pt[a] || !pt[b]) return;
        m_shadowBrush->SetColor(kShadowCol);
        m_renderTarget->DrawLine(
            D2D1::Point2F(pt[a]->x + 1.f, pt[a]->y + 1.f),
            D2D1::Point2F(pt[b]->x + 1.f, pt[b]->y + 1.f),
            m_shadowBrush.Get(), kStroke);
        m_brush->SetColor(kBoneCol);
        m_renderTarget->DrawLine(*pt[a], *pt[b], m_brush.Get(), kStroke);
    };

    // Draw a small filled dot at a joint position.
    auto Joint = [&](int i)
    {
        if (!pt[i]) return;
        m_brush->SetColor(kBoneCol);
        m_renderTarget->FillEllipse(
            D2D1::Ellipse(*pt[i], kJointR, kJointR), m_brush.Get());
    };

    // ── Spine ─────────────────────────────────────────────────────────────────
    Bone(OB::NECK,  OB::CHEST);
    Bone(OB::CHEST, OB::SPINE);
    Bone(OB::SPINE, OB::PELVIS);

    // ── Left arm ──────────────────────────────────────────────────────────────
    Bone(OB::CHEST,     OB::LSHOULDER);
    Bone(OB::LSHOULDER, OB::LELBOW);
    Bone(OB::LELBOW,    OB::LWRIST);

    // ── Right arm ─────────────────────────────────────────────────────────────
    Bone(OB::CHEST,     OB::RSHOULDER);
    Bone(OB::RSHOULDER, OB::RELBOW);
    Bone(OB::RELBOW,    OB::RWRIST);

    // ── Left leg ──────────────────────────────────────────────────────────────
    Bone(OB::PELVIS, OB::LHIP);
    Bone(OB::LHIP,   OB::LKNEE);
    Bone(OB::LKNEE,  OB::LANKLE);

    // ── Right leg ─────────────────────────────────────────────────────────────
    Bone(OB::PELVIS, OB::RHIP);
    Bone(OB::RHIP,   OB::RKNEE);
    Bone(OB::RKNEE,  OB::RANKLE);

    // ── Joints ────────────────────────────────────────────────────────────────
    Joint(OB::LSHOULDER); Joint(OB::LELBOW); Joint(OB::LWRIST);
    Joint(OB::RSHOULDER); Joint(OB::RELBOW); Joint(OB::RWRIST);
    Joint(OB::LHIP);      Joint(OB::LKNEE);  Joint(OB::LANKLE);
    Joint(OB::RHIP);      Joint(OB::RKNEE);  Joint(OB::RANKLE);
    Joint(OB::PELVIS);    Joint(OB::CHEST);

    // ── Head circle ───────────────────────────────────────────────────────────
    if (pt[OB::HEAD])
    {
        // Shadow
        m_shadowBrush->SetColor(kShadowCol);
        m_renderTarget->DrawEllipse(
            D2D1::Ellipse(D2D1::Point2F(pt[OB::HEAD]->x + 1.f,
                                        pt[OB::HEAD]->y + 1.f),
                          kHeadR, kHeadR),
            m_shadowBrush.Get(), kStroke);
        // Foreground
        m_brush->SetColor(kBoneCol);
        m_renderTarget->DrawEllipse(
            D2D1::Ellipse(*pt[OB::HEAD], kHeadR, kHeadR),
            m_brush.Get(), kStroke);
    }
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

    // Label rect: fixed width centred on the box so narrow boxes don't wrap text.
    const float cx = (box.left + box.right) * 0.5f;
    constexpr float kLabelW = 120.f;
    D2D1_RECT_F labelRect = D2D1::RectF(
        cx - kLabelW * 0.5f,
        box.top - 14.f - kNameOffsetY,
        cx + kLabelW * 0.5f,
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

// ── Renderer::DrawWeaponName ──────────────────────────────────────────────────
//
// Draws the active weapon name centred above the player name label.
// Rendered in a yellow-amber colour to distinguish it from the white name tag.

void Renderer::DrawWeaponName(const EntityData& entity, const ScreenBox& box)
{
    const wchar_t* wname = entity.WeaponName();
    if (!wname || wname[0] == L'\0') return;

    const UINT32 len = static_cast<UINT32>(wcslen(wname));

    constexpr float kLabelH  = 13.f;
    constexpr float kLabelW  = 120.f;
    // Sit above the name label (name is 14 + 3 px above box top, so weapon is another 14 px higher)
    constexpr float kNameH   = 14.f + kNameOffsetY;   // same as DrawName's reserved space
    constexpr float kGapY    = 2.f;

    const float cx = (box.left + box.right) * 0.5f;
    D2D1_RECT_F labelRect = D2D1::RectF(
        cx - kLabelW * 0.5f,
        box.top - kNameH - kLabelH - kGapY,
        cx + kLabelW * 0.5f,
        box.top - kNameH - kGapY
    );

    // Drop shadow
    m_shadowBrush->SetOpacity(0.85f);
    m_renderTarget->DrawText(wname, len, m_textFormat.Get(),
        D2D1::RectF(labelRect.left + 1.f, labelRect.top + 1.f,
                    labelRect.right + 1.f, labelRect.bottom + 1.f),
        m_shadowBrush.Get());

    // Amber / yellow foreground — easy to distinguish from the white name tag
    m_brush->SetColor(D2D1::ColorF(1.0f, 0.85f, 0.2f, 1.0f));
    m_renderTarget->DrawText(wname, len, m_textFormat.Get(), labelRect, m_brush.Get());
}

// ── Renderer::DrawHealthNumber ────────────────────────────────────────────────
//
// Renders the player's HP as a plain integer (e.g. "87") centred just below
// the bottom edge of the bounding box.  Colour matches the HP bar gradient
// (green → yellow → red) so the two elements are visually consistent.

void Renderer::DrawHealthNumber(int health, const ScreenBox& box)
{
    wchar_t buf[8];
    swprintf_s(buf, L"%d", std::clamp(health, 0, 100));
    const UINT32 len = static_cast<UINT32>(wcslen(buf));

    constexpr float kLabelH  = 13.f;   // single-line text height (px)
    constexpr float kGapY    = 2.f;    // gap between box bottom and text top
    constexpr float kNumW    = 40.f;   // fixed width — enough for "100" at 11pt

    const float cx = (box.left + box.right) * 0.5f;
    D2D1_RECT_F labelRect = D2D1::RectF(
        cx - kNumW * 0.5f,
        box.bottom + kGapY,
        cx + kNumW * 0.5f,
        box.bottom + kGapY + kLabelH
    );

    // Drop shadow (1 px offset)
    m_shadowBrush->SetOpacity(0.85f);
    D2D1_RECT_F shadowRect = D2D1::RectF(
        labelRect.left  + 1.f, labelRect.top    + 1.f,
        labelRect.right + 1.f, labelRect.bottom + 1.f
    );
    m_renderTarget->DrawText(buf, len, m_textFormat.Get(), shadowRect, m_shadowBrush.Get());

    // Foreground — same colour as the HP bar
    m_brush->SetColor(HpColour(health));
    m_renderTarget->DrawText(buf, len, m_textFormat.Get(), labelRect, m_brush.Get());
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

// ── Renderer::DrawMenu ────────────────────────────────────────────────────────
//
// Semi-transparent panel shown when the user presses INSERT.
// Displays feature state and basic key hints.

void Renderer::DrawMenu(bool visible, const wchar_t* resLabel, const Features& feat, int cursor)
{
    if (!visible) return;

    constexpr float kPanelX = 20.f;
    constexpr float kPanelY = 40.f;
    constexpr float kPanelW = 230.f;
    constexpr float kPanelH = 198.f;   // taller to fit feature rows
    constexpr float kPad    = 10.f;
    constexpr float kLineH  = 18.f;

    D2D1_RECT_F panel = D2D1::RectF(kPanelX, kPanelY,
                                     kPanelX + kPanelW, kPanelY + kPanelH);

    // Background — semi-transparent so the game is visible through the panel
    m_hpBgBrush->SetOpacity(0.45f);
    m_renderTarget->FillRectangle(panel, m_hpBgBrush.Get());

    // Border (cyan-ish)
    m_brush->SetColor(D2D1::ColorF(0.3f, 0.75f, 1.0f, 1.f));
    m_renderTarget->DrawRectangle(panel, m_brush.Get(), 1.2f);

    // ── Title ─────────────────────────────────────────────────────────────────
    {
        const wchar_t* title = L"CS2 Wallhack";
        D2D1_RECT_F r = D2D1::RectF(kPanelX + kPad, kPanelY + 6.f,
                                     kPanelX + kPanelW - kPad, kPanelY + 6.f + kLineH);
        // shadow
        m_shadowBrush->SetOpacity(0.9f);
        m_renderTarget->DrawText(title, static_cast<UINT32>(wcslen(title)),
            m_menuTextFormat.Get(),
            D2D1::RectF(r.left+1.f, r.top+1.f, r.right+1.f, r.bottom+1.f),
            m_shadowBrush.Get());
        m_brush->SetColor(D2D1::ColorF(0.3f, 0.85f, 1.0f, 1.f));
        m_renderTarget->DrawText(title, static_cast<UINT32>(wcslen(title)),
            m_menuTextFormat.Get(), r, m_brush.Get());
    }

    // Divider
    const float divY = kPanelY + 28.f;
    m_brush->SetColor(D2D1::ColorF(0.3f, 0.75f, 1.0f, 0.45f));
    m_renderTarget->DrawLine(
        D2D1::Point2F(kPanelX + kPad, divY),
        D2D1::Point2F(kPanelX + kPanelW - kPad, divY),
        m_brush.Get(), 0.8f);

    // ── Feature toggle rows ───────────────────────────────────────────────────
    {
        struct Row { const wchar_t* label; bool state; };
        Row rows[4] = {
            { L"Box",    feat.showBox    },
            { L"Name",   feat.showName   },
            { L"Health", feat.showHealth },
            { L"Weapon", feat.showWeapon },
        };

        for (int i = 0; i < 4; ++i)
        {
            float lineY = divY + 6.f + i * (kLineH + 2.f);
            bool  sel   = (cursor == i);

            // Cursor arrow
            if (sel)
            {
                m_brush->SetColor(D2D1::ColorF(0.3f, 0.85f, 1.0f, 1.f));
                const wchar_t* arrow = L"\u25BA";
                m_renderTarget->DrawText(arrow, 1, m_menuTextFormat.Get(),
                    D2D1::RectF(kPanelX + kPad, lineY,
                                kPanelX + kPad + 14.f, lineY + kLineH),
                    m_brush.Get());
            }

            // Label
            D2D1_RECT_F labelR = D2D1::RectF(kPanelX + kPad + 14.f, lineY,
                                              kPanelX + 140.f, lineY + kLineH);
            m_brush->SetColor(sel
                ? D2D1::ColorF(1.f,   1.f,  1.f,  1.f)    // bright white when selected
                : D2D1::ColorF(0.75f, 0.75f, 0.75f, 1.f)); // dim when not selected
            m_renderTarget->DrawText(rows[i].label,
                static_cast<UINT32>(wcslen(rows[i].label)),
                m_menuTextFormat.Get(), labelR, m_brush.Get());

            // ON / OFF badge
            const wchar_t* badge = rows[i].state ? L"ON" : L"OFF";
            D2D1_RECT_F badgeR = D2D1::RectF(kPanelX + 150.f, lineY,
                                              kPanelX + kPanelW - kPad, lineY + kLineH);
            m_brush->SetColor(rows[i].state
                ? D2D1::ColorF(0.27f, 1.0f, 0.27f, 1.f)
                : D2D1::ColorF(1.0f,  0.35f, 0.35f, 1.f));
            m_renderTarget->DrawText(badge,
                static_cast<UINT32>(wcslen(badge)),
                m_menuTextFormat.Get(), badgeR, m_brush.Get());
        }
    }

    // ── Resolution row ────────────────────────────────────────────────────────
    {
        bool  sel   = (cursor == 4);
        float lineY = divY + 6.f + 4 * (kLineH + 2.f) + 4.f;

        if (sel)
        {
            m_brush->SetColor(D2D1::ColorF(0.3f, 0.85f, 1.0f, 1.f));
            const wchar_t* arrow = L"\u25BA";
            m_renderTarget->DrawText(arrow, 1, m_menuTextFormat.Get(),
                D2D1::RectF(kPanelX + kPad, lineY,
                            kPanelX + kPad + 14.f, lineY + kLineH),
                m_brush.Get());
        }

        wchar_t line[64];
        swprintf_s(line, L"Res:  %s", resLabel ? resLabel : L"?");
        D2D1_RECT_F r = D2D1::RectF(kPanelX + kPad + 14.f, lineY,
                                     kPanelX + kPanelW - kPad, lineY + kLineH);
        m_brush->SetColor(sel
            ? D2D1::ColorF(0.9f, 0.75f, 0.2f, 1.f)
            : D2D1::ColorF(0.6f, 0.5f,  0.15f, 1.f));
        m_renderTarget->DrawText(line, static_cast<UINT32>(wcslen(line)),
            m_menuTextFormat.Get(), r, m_brush.Get());
    }

    // ── Credit ────────────────────────────────────────────────────────────────
    {
        const wchar_t* credit = L"Developed by JayLord";
        float lineY = divY + 6.f + 4 * (kLineH + 2.f) + 4.f + kLineH + 6.f;
        D2D1_RECT_F r = D2D1::RectF(kPanelX + kPad, lineY,
                                     kPanelX + kPanelW - kPad, lineY + kLineH);
        m_shadowBrush->SetOpacity(0.8f);
        m_renderTarget->DrawText(credit, static_cast<UINT32>(wcslen(credit)),
            m_menuTextFormat.Get(),
            D2D1::RectF(r.left+1.f, r.top+1.f, r.right+1.f, r.bottom+1.f),
            m_shadowBrush.Get());
        m_brush->SetColor(D2D1::ColorF(0.85f, 0.85f, 0.85f, 1.f));
        m_renderTarget->DrawText(credit, static_cast<UINT32>(wcslen(credit)),
            m_menuTextFormat.Get(), r, m_brush.Get());
    }

    // ── Hint ──────────────────────────────────────────────────────────────────
    {
        const wchar_t* hint = L"[\u2191\u2193] select  [\u2190\u2192] toggle  [INS] hide";
        float lineY = kPanelY + kPanelH - kLineH - 6.f;
        D2D1_RECT_F r = D2D1::RectF(kPanelX + kPad, lineY,
                                     kPanelX + kPanelW - kPad, lineY + kLineH);
        m_brush->SetColor(D2D1::ColorF(0.5f, 0.5f, 0.5f, 0.9f));
        m_renderTarget->DrawText(hint, static_cast<UINT32>(wcslen(hint)),
            m_menuTextFormat.Get(), r, m_brush.Get());
    }
}

// ── Renderer::DrawWatermark ───────────────────────────────────────────────────
//
// Persistent "Developed by JayLord" credit in the bottom-right corner.
// Always rendered regardless of menu state.

void Renderer::DrawWatermark()
{
    constexpr wchar_t kText[] = L"Developed by JayLord";
    constexpr float   kTextW  = 165.f;
    constexpr float   kTextH  = 14.f;
    constexpr float   kMargin = 10.f;

    const float x = static_cast<float>(m_width)  - kTextW - kMargin;
    const float y = static_cast<float>(m_height) - kTextH - kMargin;

    D2D1_RECT_F r = D2D1::RectF(x, y, x + kTextW, y + kTextH);

    // Drop shadow
    m_shadowBrush->SetOpacity(0.75f);
    m_renderTarget->DrawText(kText, static_cast<UINT32>(wcslen(kText)),
        m_menuTextFormat.Get(),
        D2D1::RectF(r.left+1.f, r.top+1.f, r.right+1.f, r.bottom+1.f),
        m_shadowBrush.Get());

    // Foreground (subtle grey)
    m_brush->SetColor(D2D1::ColorF(0.75f, 0.75f, 0.75f, 0.80f));
    m_renderTarget->DrawText(kText, static_cast<UINT32>(wcslen(kText)),
        m_menuTextFormat.Get(), r, m_brush.Get());
}
