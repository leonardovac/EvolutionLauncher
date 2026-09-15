#include "app/window.h"

#include "app/rail.h"
#include "app/shelllayout.h"

#include <windowsx.h>

#include <algorithm>
#include <array>

namespace app
{
namespace
{

constexpr wchar_t className[] = L"EvolutionLauncherWindow";
constexpr int dragStripHeight = 64;   // design-space; the hero top edge is draggable
constexpr DWORD idleWaitMs = 100;     // bounded so a repaint with no message still lands promptly

// mirrors shell.cpp's closeBox layout so the caption strip doesn't swallow the click
RECT closeGlyphRect(int width, float scale)
{
    const float size = shellGlyphSize * scale;
    const float x = static_cast<float>(width) - shellEdgeMargin * scale - size;
    const float y = (shellHeaderTop + (shellRowHeight - shellGlyphSize) * 0.5f) * scale;
    return RECT{static_cast<LONG>(x), static_cast<LONG>(y), static_cast<LONG>(x + size),
                static_cast<LONG>(y + size)};
}

// mirrors shell.cpp's minimiseBox, left of the close glyph
RECT minimiseGlyphRect(int width, float scale)
{
    const RECT close = closeGlyphRect(width, scale);
    const LONG shift = static_cast<LONG>(shellMinimiseGap * scale);
    return RECT{close.left - shift, close.top, close.right - shift, close.bottom};
}

// mirrors shell.cpp's languageRow; the whole row is clickable, not just the chevron
RECT languageRowRect(int width, float scale)
{
    const RECT minimiseBox = minimiseGlyphRect(width, scale);
    const LONG divider = minimiseBox.left - static_cast<LONG>(shellDividerGap * scale);
    const LONG rowRight = divider - static_cast<LONG>(shellLanguageGap * scale);
    const LONG rowLeft = rowRight - static_cast<LONG>(shellLanguageWidth * scale);
    const LONG top = static_cast<LONG>(shellHeaderTop * scale);
    return RECT{rowLeft, top, rowRight, top + static_cast<LONG>(shellRowHeight * scale)};
}

// mirrors shell.cpp's cogBox, left of the language row
RECT cogRect(int width, float scale)
{
    const RECT language = languageRowRect(width, scale);
    const LONG size = static_cast<LONG>(shellGlyphSize * scale);
    const LONG right = language.left - static_cast<LONG>(shellCogGap * scale);
    return RECT{right - size, language.top, right, language.bottom};
}

// mirrors shell.cpp's nav row, which sits inside the draggable caption strip
RECT navRowRect(int width, float scale)
{
    const RECT language = languageRowRect(width, scale);
    const LONG left = static_cast<LONG>((railWidth + shellContentPad) * scale);
    const LONG top = static_cast<LONG>(shellHeaderTop * scale);
    return RECT{left, top, language.left - static_cast<LONG>(24.f * scale),
                top + static_cast<LONG>(shellRowHeight * scale)};
}

// the taskbar's screen, the one the user is working on
RECT workArea()
{
    POINT cursor{};
    ::GetCursorPos(&cursor);
    HMONITOR monitor = ::MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO info{sizeof(info)};
    if (::GetMonitorInfoW(monitor, &info) == 0)
        return RECT{0, 0, 0, 0};
    return info.rcWork;
}

// the window is a WS_POPUP, so CW_USEDEFAULT would place it at 0,0
POINT centredOrigin(const RECT& work, int width, int height)
{
    const LONG x = work.left + ((work.right - work.left) - width) / 2;
    const LONG y = work.top + ((work.bottom - work.top) - height) / 2;
    return POINT{(std::max)(work.left, x), (std::max)(work.top, y)};
}

// a small screen or a high DPI would otherwise put the foot of the window past the taskbar
float fittedScale(const RECT& work, int width, int height, float dpiScale)
{
    const float room = 0.94f;
    const float byWidth = static_cast<float>(work.right - work.left) * room / width;
    const float byHeight = static_cast<float>(work.bottom - work.top) * room / height;
    return (std::min)(dpiScale, (std::min)(byWidth, byHeight));
}

}

LRESULT CALLBACK Window::proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    auto* self = reinterpret_cast<Window*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE)
    {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lp);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        return ::DefWindowProcW(hwnd, msg, wp, lp);
    }
    return self != nullptr ? self->handle(hwnd, msg, wp, lp) : ::DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT Window::handle(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_NCHITTEST:
    {
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ::ScreenToClient(hwnd, &pt);
        const int strip = static_cast<int>(dragStripHeight * scale_);
        if (pt.y >= strip)
            return HTCLIENT;
        const std::array glyphs{closeGlyphRect(width_, scale_), minimiseGlyphRect(width_, scale_),
                                languageRowRect(width_, scale_), cogRect(width_, scale_),
                                navRowRect(width_, scale_)};
        const bool onGlyph =
            std::ranges::any_of(glyphs, [&pt](const RECT& r) { return ::PtInRect(&r, pt) != 0; });
        return onGlyph ? HTCLIENT : HTCAPTION;
    }
    case WM_SIZE:
        width_ = LOWORD(lp);
        height_ = HIWORD(lp);
        resized_ = true;
        return 0;
    case WM_DPICHANGED:
    {
        scale_ = static_cast<float>(HIWORD(wp)) / 96.f;
        scaleChanged_ = true;
        const RECT* target = reinterpret_cast<const RECT*>(lp);
        ::SetWindowPos(hwnd, nullptr, target->left, target->top, target->right - target->left,
                       target->bottom - target->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_MOUSEMOVE:
        mousePos_ = {static_cast<float>(GET_X_LPARAM(lp)), static_cast<float>(GET_Y_LPARAM(lp))};
        return 0;
    case WM_LBUTTONDOWN:
        mousePos_ = {static_cast<float>(GET_X_LPARAM(lp)), static_cast<float>(GET_Y_LPARAM(lp))};
        mouseDown_ = true;
        mousePressed_ = true;
        ::SetCapture(hwnd);
        return 0;
    case WM_LBUTTONUP:
        mousePos_ = {static_cast<float>(GET_X_LPARAM(lp)), static_cast<float>(GET_Y_LPARAM(lp))};
        mouseDown_ = false;
        mouseReleased_ = true;
        ::ReleaseCapture();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return ::DefWindowProcW(hwnd, msg, wp, lp);
}

bool Window::create(int width, int height)
{
    ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &Window::proc;
    wc.hInstance = ::GetModuleHandleW(nullptr);
    wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = className;
    ::RegisterClassExW(&wc);

    width_ = width;
    height_ = height;
    const RECT work = workArea();
    POINT origin = centredOrigin(work, width, height);
    hwnd_ = ::CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_APPWINDOW, className, L"Warframe",
                              WS_POPUP, origin.x, origin.y, width, height, nullptr, nullptr,
                              wc.hInstance, this);
    if (hwnd_ == nullptr)
        return false;

    scale_ = fittedScale(work, width, height,
                         static_cast<float>(::GetDpiForWindow(hwnd_)) / 96.f);
    const int scaledW = static_cast<int>(width * scale_);
    const int scaledH = static_cast<int>(height * scale_);
    origin = centredOrigin(work, scaledW, scaledH);
    ::SetWindowPos(hwnd_, nullptr, origin.x, origin.y, scaledW, scaledH,
                   SWP_NOZORDER | SWP_NOACTIVATE);
    ::ShowWindow(hwnd_, SW_SHOW);
    return true;
}

void Window::destroy()
{
    if (hwnd_ != nullptr)
    {
        ::DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

void Window::minimise() const noexcept
{
    if (hwnd_ != nullptr)
        ::ShowWindow(hwnd_, SW_MINIMIZE);
}

bool Window::pump()
{
    MSG msg{};
    while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
    {
        if (msg.message == WM_QUIT)
            running_ = false;
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }
    return running_;
}

void Window::waitForInput() const noexcept
{
    // MWMO_INPUTAVAILABLE so a message that arrived since the last pump still wakes us
    ::MsgWaitForMultipleObjectsEx(0, nullptr, idleWaitMs, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
}

bool Window::takeResized() noexcept
{
    const bool was = resized_;
    resized_ = false;
    return was;
}

bool Window::takeScaleChanged() noexcept
{
    const bool was = scaleChanged_;
    scaleChanged_ = false;
    return was;
}

void Window::clearMouseEdge() noexcept
{
    mousePressed_ = false;
    mouseReleased_ = false;
}

}
