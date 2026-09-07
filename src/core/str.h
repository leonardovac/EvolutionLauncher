#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace core
{

std::string narrow(std::wstring_view text);
std::wstring widen(std::string_view text);

std::string hex(std::span<const std::uint8_t> bytes);
std::optional<std::array<std::uint8_t, 16>> parseHash(std::wstring_view text);

std::wstring lower(std::wstring_view text);

bool equalsNoCase(std::wstring_view a, std::wstring_view b);
bool containsNoCase(std::wstring_view haystack, std::wstring_view needle);
bool startsWithNoCase(std::wstring_view text, std::wstring_view prefix);
bool endsWithNoCase(std::wstring_view text, std::wstring_view suffix);

std::string formatBytes(std::uint64_t bytes);

}
