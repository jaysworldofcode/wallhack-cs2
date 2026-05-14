#include "overlay.hpp"

// ── Overlay::Create ───────────────────────────────────────────────────────────

bool Overlay::Create(int width, int height, std::wstring_view title)
{
    m_hInst  = GetModuleHandleW(nullptr);
    m_width  = width;
    m_height = height;

    // ── Register window class ─────────────────────────────────────────────────

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = m_hInst;
    wc.lpszClassName = kClassName;
    // Solid black background: GDI fills with black before DWM compositing.
    // DWM later replaces the black with transparency (via the glass extension).
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));

    if (!RegisterClassExW(&wc))
    {
        // Class may already be registered from a previous run; that is fine.
        if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return false;
    }

    // ── Create window ─────────────────────────────────────────────────────────
    //
    // WS_EX_LAYERED     — required for transparency / compositing tricks
    // WS_EX_TRANSPARENT — mouse events pass through to the window beneath
    // WS_EX_TOPMOST     — always rendered above normal windows (incl. game)
    // WS_EX_NOACTIVATE  — never steals keyboard focus from the game
    //
    // WS_POPUP          — no title bar, no border
    //

    m_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
        kClassName,
        title.data(),
        WS_POPUP,
        0, 0, width, height,
        nullptr, nullptr,
        m_hInst,
        nullptr
    );

    if (!m_hwnd)
        return false;

    // ── DWM glass extension ───────────────────────────────────────────────────
    //
    // Setting all four margins to -1 extends the DWM frame across the entire
    // client area.  This is the documented way to achieve a fully transparent
    // window that still supports per-pixel alpha in Direct2D.
    //
    MARGINS margins{ -1, -1, -1, -1 };
    DwmExtendFrameIntoClientArea(m_hwnd, &margins);

    // ── Layered window alpha ──────────────────────────────────────────────────
    // LWA_ALPHA with bAlpha=255 means "fully opaque layered window".
    // The per-pixel alpha from Direct2D (alpha channel of each drawn pixel)
    // still controls individual pixel transparency after DWM compositing.
    SetLayeredWindowAttributes(m_hwnd, 0, 255, LWA_ALPHA);

    ShowWindow(m_hwnd, SW_SHOW);
    UpdateWindow(m_hwnd);

    return true;
}

// ── Overlay::Destroy ──────────────────────────────────────────────────────────

void Overlay::Destroy()
{
    if (m_hwnd)
    {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    UnregisterClassW(kClassName, m_hInst);
}

// ── Overlay::PumpMessages ─────────────────────────────────────────────────────

bool Overlay::PumpMessages()
{
    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
    {
        if (msg.message == WM_QUIT)
            return false;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return true;
}

// ── Overlay::WndProc ──────────────────────────────────────────────────────────

LRESULT CALLBACK Overlay::WndProc(HWND hwnd, UINT msg,
                                   WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
        // Prevent Windows from painting the background and erasing the D2D
        // content — the renderer clears to transparent each frame instead.
        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT:
        {
            // Validate the paint region without actually painting; the D2D
            // renderer drives all drawing outside the WM_PAINT message loop.
            PAINTSTRUCT ps{};
            BeginPaint(hwnd, &ps);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_KEYDOWN:
            // Allow INSERT or END key to close the overlay cleanly.
            if (wParam == VK_INSERT || wParam == VK_END)
                PostQuitMessage(0);
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}
