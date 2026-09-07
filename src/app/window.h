#pragma once

#include <windows.h>

#include <string>

namespace app
{

class Window
{
public:
    bool create(int width, int height);
    void destroy();

    [[nodiscard]] HWND handle() const noexcept { return hwnd_; }
    [[nodiscard]] int width() const noexcept { return width_; }
    [[nodiscard]] int height() const noexcept { return height_; }
    [[nodiscard]] float scale() const noexcept { return scale_; }

    bool pump();
    bool takeResized() noexcept;

private:
    static LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT handle(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    HWND hwnd_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    float scale_ = 1.f;
    bool resized_ = false;
    bool running_ = true;
};

}
