#pragma once

#include "app/updatejob.h"
#include "core/types.h"
#include "gfx/image.h"

#include <string>

namespace app
{

struct ShellState
{
    JobPhase phase = JobPhase::Idle;
    std::string buildLabel;
    std::string statusLine;
    float progress = 0.f;
    bool startEnabled = false;
};

void drawShell(const core::Rect& viewport, gfx::Image* hero, const ShellState& state);
bool shellCloseClicked();
bool shellStartClicked();

}
