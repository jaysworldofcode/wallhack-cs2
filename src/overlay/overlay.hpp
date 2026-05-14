#pragma once
//
// overlay.hpp — Transparent, click-through Win32 overlay window
//
// Technique: DWM frame extension + Direct2D
// ─────────────────────────────────────────
// 1. Create a borderless popup window covering the game monitor.
// 2. Extend the DWM (Desktop Window Manager) glass frame to fill the entire
//    client area using DwmExtendFrameIntoClientArea with margins {-1}.
//    This makes the window background fully transparent to compositing.
// 3. Mark the window WS_EX_TRANSPARENT so mouse events fall through to the
//    game window underneath.
// 4. The Direct2D render target clears to (0,0,0,0) each frame, giving a
//    fully transparent canvas on which ESP elements are drawn.
//
// Window class / handle ownership lives here; the Renderer holds the D2D
// resources that draw onto this HWND.
//

#include <Windows.h>
#include <dwmapi.h>
#include <string_view>

class Overlay
{
public:
    /// Creates and shows the overlay window.
    /// @param width  Desired client width  (should match game resolution).
    /// @param height Desired client height (should match game resolution).
    /// @returns true on success.
    bool Create(int width, int height, std::wstring_view title = L"overlay");

    /// Destroy the window and unregister the window class.
    void Destroy();

    /// Process pending Windows messages.
    /// @returns false if a WM_QUIT has been received (time to exit).
    bool PumpMessages();

    HWND Hwnd()   const { return m_hwnd;   }
    int  Width()  const { return m_width;  }
    int  Height() const { return m_height; }

private:
    HWND       m_hwnd    = nullptr;
    HINSTANCE  m_hInst   = nullptr;
    int        m_width   = 0;
    int        m_height  = 0;

    static constexpr wchar_t kClassName[] = L"CS2OverlayClass";

    // Window procedure (static trampoline → member dispatcher).
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg,
                                    WPARAM wParam, LPARAM lParam);
};
