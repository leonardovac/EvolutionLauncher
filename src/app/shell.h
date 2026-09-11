#pragma once

#include "app/updatejob.h"
#include "core/types.h"
#include "gfx/image.h"

#include <string_view>

namespace app
{

// the views must outlive the drawShell call; the caller owns their storage
struct ShellState
{
    JobPhase phase = JobPhase::Idle;
    std::string_view buildLabel;
    std::string_view statusLine;
    std::string_view fileLine;
    float progress = 0.f;
    bool startEnabled = false;
};

void drawShell(const core::Rect& viewport, gfx::Image* hero, const ShellState& state);
bool shellCloseClicked();
bool shellStartClicked();

}
