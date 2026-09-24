#pragma once

#include "core/types.h"
#include "gfx/font.h"
#include "ui/ui.h"

#include <array>
#include <span>
#include <string_view>

namespace app
{

// Font::measure is untracked, so the gaps between glyphs have to be added back
float trackedWidth(gfx::Font& font, std::string_view text, float tracking);

// calls draw(offset, colour) once per halo tap; the caller then draws the glyph itself on top
template <class Draw>
void glyphShadow(Draw&& draw)
{
    const float d = ui::px(1.f);
    const std::array offsets{core::Vec2(-d, 0.f), core::Vec2(d, 0.f), core::Vec2(0.f, -d),
                             core::Vec2(0.f, d), core::Vec2(-d, d), core::Vec2(d, d),
                             core::Vec2(0.f, d * 2.f)};
    const core::Col col = ui::theme().scrim.alpha(0.3f);
    for (const core::Vec2& offset : offsets)
        draw(offset, col);
}

enum class DropdownGroup
{
    Shell,
    Settings
};

// returns true when the value changed this frame
bool checkbox(std::string_view id, const core::Rect& row, std::string_view label, bool& value);

// listWidth defaults to the field's own width; a wider list extends left of the field's right edge.
// A Shell chip centres glyph, value and chevron as one group and reports the glyph slot in outLead.
void dropdown(DropdownGroup group, std::string_view id, const core::Rect& row,
              std::string_view label, std::span<const std::string_view> options, int& index,
              std::string_view display = {}, float listWidth = 0.f,
              core::Rect* outLead = nullptr);

// draws the expanded list only when `group` owns the open dropdown; call once per group
bool dropdownOverlay(DropdownGroup group);

void closeDropdown();

bool dropdownOpen();

// records the hint for the hovered row; tooltipOverlay draws it once, after every row
void tooltip(const core::Rect& row, std::string_view text);
void tooltipOverlay(const core::Rect& bounds);

}
