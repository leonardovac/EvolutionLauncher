#pragma once

#include "update/plan.h"

#include <optional>
#include <span>
#include <string_view>

namespace app
{

struct NavEntry
{
    std::string_view id;
    std::string_view label;
    std::string_view url;
};

struct TitleProfile
{
    wf::Title title;
    // HKCU-relative; the stock launcher's own key, which also holds the game settings
    std::wstring_view registrySubkey;
    std::wstring_view exeName;
    // under %LOCALAPPDATA%, used only when LauncherExe names no usable root
    std::wstring_view localFolder;
    std::string_view railId;
    std::string_view label;
    std::span<const NavEntry> nav;
    int heroResource;
    int iconResource;
};

const TitleProfile& profile(wf::Title title);
std::span<const TitleProfile> titleProfiles();
std::optional<wf::Title> parseTitleName(std::wstring_view text);

}
