#pragma once

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
};

int run(const Options& options);

}
