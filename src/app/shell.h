#pragma once

#include "app/jobphase.h"
#include "app/titles.h"
#include "core/types.h"
#include "gfx/image.h"

#include <span>
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
    std::span<const NavEntry> nav;
    // null when the selected title has no ornament
    gfx::Image* leaf = nullptr;
    gfx::Image* endCap = nullptr;
    int languageIndex = 0;
    float progress = 0.f;
    std::string_view startLabel = "PLAY";
    bool startEnabled = false;
    // the quiet line under the button, shown only while an update is pending
    bool secondaryVisible = false;
    std::string_view secondaryLabel = "PLAY WITHOUT UPDATING";
    bool panelVisible = false;
};

// `live` is the fetched key art fading in over the baked-in `base`
struct HeroFrame
{
    gfx::Image* base = nullptr;
    gfx::Image* live = nullptr;
    float fade = 0.f;
};

void drawShell(const core::Rect& viewport, const HeroFrame& hero, const ShellState& state);

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
// the axis the status line and the PLAY button share; set by drawShell, read by the rail's cog
float shellFooterAxis();

}
