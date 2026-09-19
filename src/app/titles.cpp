#include "app/titles.h"

#include "app/resource.h"
#include "core/str.h"

#include <array>

namespace app
{
namespace
{

constexpr std::array<NavEntry, 3> warframeNav{
    {{"shell.nav.news", "NEWS", "https://www.warframe.com/news"},
     {"shell.nav.notes", "PATCH NOTES", "https://www.warframe.com/patch-notes"},
     {"shell.nav.prime", "PRIME ACCESS", "https://www.warframe.com/prime-access"}}};

constexpr std::array<NavEntry, 3> soulframeNav{
    {{"shell.nav.preludes", "PRELUDES", "https://www.soulframe.com/en/news"},
     {"shell.nav.buildnotes", "BUILD NOTES", "https://www.soulframe.com/en/patch-notes"},
     {"shell.nav.community", "COMMUNITY", "https://www.soulframe.com/en/community"}}};

constexpr std::array<TitleProfile, 2> profiles{
    {{wf::Title::Warframe, L"Software\\Digital Extremes\\Warframe\\Launcher",
      L"Warframe.x64.exe", L"Warframe", "rail.warframe", "WARFRAME", "PLAY", warframeNav,
      RES_WARFRAME_HERO, RES_WARFRAME_ICON},
     {wf::Title::Soulframe, L"Software\\Digital Extremes\\Soulframe\\Launcher",
      L"Soulframe.x64.exe", L"Soulframe", "rail.soulframe", "SOULFRAME", "ENTER", soulframeNav,
      RES_SOULFRAME_HERO, RES_SOULFRAME_ICON, RES_SOULFRAME_LEAF, RES_SOULFRAME_ENDCAP}}};

}

// profile() indexes by the enum, so the table order is load-bearing
static_assert(profiles.size() == 2, "one profile per wf::Title");
static_assert(profiles[static_cast<std::size_t>(wf::Title::Warframe)].title
              == wf::Title::Warframe);
static_assert(profiles[static_cast<std::size_t>(wf::Title::Soulframe)].title
              == wf::Title::Soulframe);

const TitleProfile& profile(wf::Title title)
{
    const auto index = static_cast<std::size_t>(title);
    return profiles[index < profiles.size() ? index : 0];
}

std::span<const TitleProfile> titleProfiles()
{
    return profiles;
}

std::optional<wf::Title> parseTitleName(std::wstring_view text)
{
    for (const TitleProfile& entry : profiles)
    {
        if (core::equalsNoCase(text, entry.localFolder))
            return entry.title;
    }
    return std::nullopt;
}

}
