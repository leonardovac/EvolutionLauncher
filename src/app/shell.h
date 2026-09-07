#pragma once

#include "core/types.h"
#include "gfx/image.h"

#include <string>

namespace app
{

struct ShellState
{
    std::string buildLabel;
    std::string statusLine;
    float progress = 0.f;
    bool showProgress = true;
    bool startEnabled = false;
};

void drawShell(const core::Rect& viewport, gfx::Image* hero, const ShellState& state);
bool shellCloseClicked();

}
