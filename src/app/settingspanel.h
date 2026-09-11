#pragma once

#include "app/settings.h"
#include "core/types.h"

namespace app
{

enum class PanelResult
{
    None,
    Accepted,
    Cancelled
};

// slide is 0 (offscreen) to 1 (fully open)
PanelResult drawSettingsPanel(const core::Rect& viewport, float slide, Settings& working);

}
