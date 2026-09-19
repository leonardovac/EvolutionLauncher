#pragma once

#include "update/plan.h"

#include <optional>
#include <string>

namespace app
{

struct Options
{
    std::wstring shotPath;
    float shotTime = 0.6f;
    bool wantShot = false;
    bool wantPanel = false;
    bool wantMenu = false;
    // capture only: which tab the shot opens on, empty when -title was not given
    std::optional<wf::Title> shotTitle;
};

int run(const Options& options);

}
