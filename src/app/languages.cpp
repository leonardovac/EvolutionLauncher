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
    // the game ships voice over for these only; every other locale plays the English audio
    bool voiceOver = false;
};

constexpr std::array<Language, 15> languages{{{L"zh", "Chinese (Simplified)", "ZH", true},
                                              {L"tc", "Chinese (Traditional)", "TC"},
                                              {L"en", "English", "EN", true},
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

constexpr std::size_t voiceOverCount()
{
    std::size_t count = 0;
    for (const Language& language : languages)
        if (language.voiceOver)
            ++count;
    return count;
}

constexpr std::size_t audioCount = voiceOverCount() + 1;

// indices into `languages`, so a code round-trips through the shorter audio list
std::array<std::size_t, audioCount - 1> makeVoiceOver()
{
    std::array<std::size_t, audioCount - 1> out{};
    std::size_t slot = 0;
    for (std::size_t i = 0; i < languages.size(); ++i)
        if (languages[i].voiceOver)
            out[slot++] = i;
    return out;
}

const std::array<std::size_t, audioCount - 1> voiceOver = makeVoiceOver();

std::array<std::string_view, audioCount> makeAudioNames()
{
    std::array<std::string_view, audioCount> names{};
    names[0] = "Default";
    for (std::size_t i = 0; i < voiceOver.size(); ++i)
        names[i + 1] = languages[voiceOver[i]].name;
    return names;
}

const std::array<std::string_view, 15> names = makeNames();
const std::array<std::string_view, audioCount> audioNames = makeAudioNames();

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
    for (std::size_t i = 0; i < voiceOver.size(); ++i)
        if (languages[voiceOver[i]].code == code)
            return static_cast<int>(i) + 1;
    return 0;
}

std::wstring_view audioLanguageCodeFromIndex(int index)
{
    if (index <= 0 || index > static_cast<int>(voiceOver.size()))
        return L"";
    return languages[voiceOver[static_cast<std::size_t>(index - 1)]].code;
}

std::string_view languageShortLabel(int index)
{
    if (index < 0 || index >= static_cast<int>(languages.size()))
        return "EN";
    return languages[static_cast<std::size_t>(index)].shortLabel;
}

}
