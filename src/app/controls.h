#pragma once

#include "core/types.h"

#include <span>
#include <string_view>

namespace app
{

// returns true when the value changed this frame
bool checkbox(std::string_view id, const core::Rect& row, std::string_view label, bool& value);

void dropdown(std::string_view id, const core::Rect& row, std::string_view label,
              std::span<const std::string_view> options, int& index);

// draws the expanded list for whichever dropdown is open; call once after every row
bool dropdownOverlay();

}
