#pragma once

#include "core/types.h"
#include "gfx/image.h"

#include <span>
#include <string_view>

namespace app
{

// design-space; drawShell insets its content by this much
inline constexpr float railWidth = 96.f;

// the cog's centre, up from the rail's own bottom edge; the rail never reads the content layout
inline constexpr float railCogInset = 54.f;

struct RailTitle
{
    std::string_view id;
    std::string_view label;
    gfx::Image* icon = nullptr;
    // the baked-in art for this title, drawn until its live key art arrives
    gfx::Image* hero = nullptr;
};

struct RailResult
{
    // the title clicked this frame, -1 when none was
    int titleClicked = -1;
    bool cogClicked = false;
};

RailResult drawRail(const core::Rect& viewport, bool inputEnabled, gfx::Image* publisher,
                    std::span<const RailTitle> titles, int selected);

}
