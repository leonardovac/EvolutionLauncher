#pragma once

#include "app/settings.h"
#include "app/updatejob.h"
#include "core/types.h"

#include <string>

namespace app
{

enum class PanelTab
{
    Settings,
    Maintenance,
    Launcher,
    // reached from the shell's update line, not from the tab strip
    Files
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
    // why the last folder pick was refused, and which folder it was; empty once one is taken
    std::string rootLine;
    std::string rootPick;
    QueuedRows files;
    std::string filesLine;
    float filesScroll = 0.f;
    // the grab point inside the scrollbar thumb while it is dragged, negative when it is not
    float filesGrab = -1.f;
};

// slide is 0 (offscreen) to 1 (fully open); a tab click is written straight into state.tab.
// Draws over the hero the shell already laid down, so it must run after drawShell
PanelAction drawPanel(const core::Rect& viewport, float slide, PanelState& state,
                      Settings& working, float wheel);

}
