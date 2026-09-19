#pragma once

#include "core/types.h"
#include "update/plan.h"

namespace app
{

struct TitleTheme
{
    core::Col accent;
    core::Col scrim;
    core::Col panelFill;
};

const TitleTheme& titleTheme();
core::Col accent();
void setTitleTheme(wf::Title title);

}
