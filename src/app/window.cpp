#include "app/window.h"

#include <windowsx.h>

namespace app
{
namespace
{

constexpr wchar_t className[] = L"WFLauncherWindow";
constexpr int dragStripHeight = 64;   // design-space; the hero top edge is draggable

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
        return pt.y < strip ? HTCAPTION : HTCLIENT;
    }
    case WM_SIZE:
        width_ = LOWORD(lp);
        height_ = HIWORD(lp);
        resized_ = true;
        return 0;
    case WM_DPICHANGED:
    {
        scale_ = static_cast<float>(HIWORD(wp)) / 96.f;
        const RECT* target = reinterpret_cast<const RECT*>(lp);
        ::SetWindowPos(hwnd, nullptr, target->left, target->top, target->right - target->left,
                       target->bottom - target->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
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
    hwnd_ = ::CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_APPWINDOW, className, L"Warframe",
                              WS_POPUP, CW_USEDEFAULT, CW_USEDEFAULT, width, height, nullptr,
                              nullptr, wc.hInstance, this);
    if (hwnd_ == nullptr)
        return false;

    scale_ = static_cast<float>(::GetDpiForWindow(hwnd_)) / 96.f;
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

bool Window::takeResized() noexcept
{
    const bool was = resized_;
    resized_ = false;
    return was;
}

}
