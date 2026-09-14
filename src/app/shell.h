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
    std::string_view detailLine;
    int languageIndex = 0;
    float progress = 0.f;
    std::string_view startLabel = "PLAY";
    bool startEnabled = false;
    // the quiet "play anyway" line under the button, shown only while an update is pending
    bool secondaryVisible = false;
    bool panelVisible = false;
};

void drawShell(const core::Rect& viewport, gfx::Image* hero, const ShellState& state);

// window chrome, not content: drawn after the panel so it never ghosts through and stays usable
void drawWindowControls(const core::Rect& viewport);
bool shellCloseClicked();
bool shellStartClicked();
bool shellSecondaryClicked();
bool shellMinimiseClicked();
int shellLanguageIndex();
bool shellCogClicked();
// the URL of the nav entry clicked this frame, empty when none was
std::string_view shellNavClicked();

}
