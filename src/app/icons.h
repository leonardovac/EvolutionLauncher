#pragma once

#include "core/types.h"

#include <string_view>

struct ID3D11Device;

namespace app
{

// private-use codepoints shared by Segoe Fluent Icons and Segoe MDL2 Assets
namespace icon
{
inline constexpr std::string_view globe = "\uE774";
inline constexpr std::string_view cog = "\uE713";
inline constexpr std::string_view minimise = "\uE921";
inline constexpr std::string_view close = "\uE8BB";
}

enum class IconSize
{
    Caption,
    Rail
};

// false when neither icon family is installed; callers keep their vector fallback
bool buildIcons(ID3D11Device* dev, float scale);
void destroyIcons();
bool iconsReady();
void drawIcon(IconSize size, std::string_view glyph, const core::Rect& box, const core::Col& col);

}
