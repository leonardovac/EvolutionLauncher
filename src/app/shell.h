#pragma once

#include "app/jobphase.h"
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
    int languageIndex = 0;
    float progress = 0.f;
    bool startEnabled = false;
    bool panelVisible = false;
};

void drawShell(const core::Rect& viewport, gfx::Image* hero, const ShellState& state);
bool shellCloseClicked();
bool shellStartClicked();
bool shellMinimiseClicked();
int shellLanguageIndex();
// the URL of the nav entry clicked this frame, empty when none was
std::string_view shellNavClicked();

}
