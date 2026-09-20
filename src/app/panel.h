#pragma once

#include "app/settings.h"
#include "core/types.h"

#include <string>

namespace app
{

enum class PanelTab
{
    Settings,
    Maintenance,
    Launcher
};

enum class PanelAction
{
    None,
    Accept,
    Dismiss,
    Verify,
    StaleReport,
    Defragment,
    LocateRoot
};

struct PanelState
{
    PanelTab tab = PanelTab::Settings;
    bool saveFailed = false;
    std::string launcherLine;
    std::string gameBuildLine;
    std::string staleLine;
    bool staleRunning = false;
    std::string defragLine;
    // what the last folder pick found; the root itself is read off the working settings
    std::string rootLine;
};

// slide is 0 (offscreen) to 1 (fully open); a tab click is written straight into state.tab.
// Draws over the hero the shell already laid down, so it must run after drawShell
PanelAction drawPanel(const core::Rect& viewport, float slide, PanelState& state,
                      Settings& working);

}
