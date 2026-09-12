#include "app/languages.h"

#include <algorithm>
#include <array>
#include <iterator>
#include <ranges>

namespace app
{
namespace
{

struct Language
{
    std::wstring_view code;
    std::string_view name;
    std::string_view shortLabel;
};

constexpr std::array<Language, 15> languages{{{L"zh", "Chinese (Simplified)", "ZH"},
                                              {L"tc", "Chinese (Traditional)", "TC"},
                                              {L"en", "English", "EN"},
                                              {L"fr", "French", "FR"},
                                              {L"de", "German", "DE"},
                                              {L"it", "Italian", "IT"},
                                              {L"ja", "Japanese", "JA"},
                                              {L"ko", "Korean", "KO"},
                                              {L"pl", "Polish", "PL"},
                                              {L"pt", "Portuguese", "PT"},
                                              {L"ru", "Russian", "RU"},
                                              {L"es", "Spanish", "ES"},
                                              {L"th", "Thai", "TH"},
                                              {L"tr", "Turkish", "TR"},
                                              {L"uk", "Ukrainian", "UK"}}};

std::array<std::string_view, 15> makeNames()
{
    std::array<std::string_view, 15> names{};
    for (std::size_t i = 0; i < languages.size(); ++i)
        names[i] = languages[i].name;
    return names;
}

std::array<std::string_view, 16> makeAudioNames()
{
    std::array<std::string_view, 16> names{};
    names[0] = "Default";
    for (std::size_t i = 0; i < languages.size(); ++i)
        names[i + 1] = languages[i].name;
    return names;
}

const std::array<std::string_view, 15> names = makeNames();
const std::array<std::string_view, 16> audioNames = makeAudioNames();

}

std::span<const std::string_view> languageNames()
{
    return names;
}

std::span<const std::string_view> audioLanguageNames()
{
    return audioNames;
}

int languageIndexFromCode(std::wstring_view code)
{
    const auto it = std::ranges::find(languages, code, &Language::code);
    if (it != languages.end())
        return static_cast<int>(std::distance(languages.begin(), it));
    const auto en = std::ranges::find(languages, std::wstring_view(L"en"), &Language::code);
    return static_cast<int>(std::distance(languages.begin(), en));
}

std::wstring_view languageCodeFromIndex(int index)
{
    if (index < 0 || index >= static_cast<int>(languages.size()))
        return L"en";
    return languages[static_cast<std::size_t>(index)].code;
}

int audioLanguageIndexFromCode(std::wstring_view code)
{
    if (code.empty())
        return 0;
    const auto it = std::ranges::find(languages, code, &Language::code);
    if (it == languages.end())
        return 0;
    return static_cast<int>(std::distance(languages.begin(), it)) + 1;
}

std::wstring_view audioLanguageCodeFromIndex(int index)
{
    if (index <= 0 || index > static_cast<int>(languages.size()))
        return L"";
    return languages[static_cast<std::size_t>(index - 1)].code;
}

std::string_view languageShortLabel(int index)
{
    if (index < 0 || index >= static_cast<int>(languages.size()))
        return "EN";
    return languages[static_cast<std::size_t>(index)].shortLabel;
}

}
