#pragma once

#include "core/types.h"

namespace app
{

// design-space; drawShell insets its content by this much
inline constexpr float railWidth = 96.f;

struct RailResult
{
    bool cogClicked = false;
};

RailResult drawRail(const core::Rect& viewport, bool inputEnabled);

}
