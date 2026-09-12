#pragma once

#include "core/types.h"

#include <span>
#include <string_view>

namespace app
{

enum class DropdownGroup
{
    Shell,
    Settings
};

// returns true when the value changed this frame
bool checkbox(std::string_view id, const core::Rect& row, std::string_view label, bool& value);

void dropdown(DropdownGroup group, std::string_view id, const core::Rect& row,
              std::string_view label, std::span<const std::string_view> options, int& index);

// draws the expanded list only when `group` owns the open dropdown; call once per group
bool dropdownOverlay(DropdownGroup group);

void closeDropdown();

}
