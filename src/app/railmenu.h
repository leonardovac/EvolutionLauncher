#pragma once

#include "core/types.h"

#include <string>

namespace app
{

enum class MenuView
{
    Rows,
    Versions,
    Optimize
};

enum class MenuAction
{
    None,
    Settings,
    Verify,
    Optimize,
    Versions,
    Back,
    Dismiss
};

struct MenuState
{
    MenuView view = MenuView::Rows;
    std::string launcherLine;
    std::string engineLine;
    std::string optimizeLine;
    bool optimizeRunning = false;
};

// slide is 0 (offscreen) to 1 (fully open)
MenuAction drawRailMenu(const core::Rect& viewport, float slide, const MenuState& state);

}
