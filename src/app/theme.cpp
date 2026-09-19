#include "app/theme.h"

#include "ui/ui.h"

#include <array>

namespace app
{
namespace
{

// Soulframe's values come off its own art: the end cap's cream and the leaf's dark outline
constexpr std::array<TitleTheme, 2> themes{
    {{core::Col::hex(0xD9C07A, 1.f), core::Col::hex(0x060505, 1.f),
      core::Col::hex(0x0F0E0E, 1.f)},
     {core::Col::hex(0xCFC099, 1.f), core::Col::hex(0x191310, 1.f),
      core::Col::hex(0x1A1512, 1.f)}}};

TitleTheme current = themes[0];

}

const TitleTheme& titleTheme()
{
    return current;
}

core::Col accent()
{
    return current.accent;
}

void setTitleTheme(wf::Title title)
{
    const auto index = static_cast<std::size_t>(title);
    current = themes[index < themes.size() ? index : 0];
    // the vendored theme is a mutable global, so the palette reaches ui/ without editing it
    ui::theme().scrim = current.scrim;
    ui::theme().panelFill = current.panelFill;
}

}
