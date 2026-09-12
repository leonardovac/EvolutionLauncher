#pragma once

#include <span>
#include <string_view>

namespace app
{

std::span<const std::string_view> languageNames();
std::span<const std::string_view> audioLanguageNames();

int languageIndexFromCode(std::wstring_view code);
std::wstring_view languageCodeFromIndex(int index);

int audioLanguageIndexFromCode(std::wstring_view code);
std::wstring_view audioLanguageCodeFromIndex(int index);

// uppercase two-letter code for the shell chip, e.g. "EN"
std::string_view languageShortLabel(int index);

}
