#include "app/settingspanel.h"

#include "app/controls.h"
#include "app/languages.h"
#include "ui/ui.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iterator>
#include <string>

namespace app
{
namespace
{

const core::Col gold = core::Col::hex(0xD9C07A, 1.f);

bool panelButton(std::string_view id, const core::Rect& box, std::string_view label)
{
    const std::uint32_t widget = ui::id(id);
    const bool hit = ui::clicked(widget, box);
    const bool hot = ui::hovered(widget, box);
    ui::dl().border(box, hot ? gold : gold.alpha(0.8f), ui::px(1.f), 0.f);
    ui::text(ui::fonts().caption, box, label, gold, ui::AlignH::Center, ui::AlignV::Middle,
             ui::px(2.f));
    return hit;
}

constexpr std::array<std::string_view, 2> graphicsApiNames{"DirectX 11", "DirectX 12"};
constexpr std::array<std::string_view, 3> gpuPreferenceNames{"Let Windows Decide", "Power Saving",
                                                              "High Performance"};
constexpr std::array<std::string_view, 3> windowModeNames{"Windowed", "Fullscreen",
                                                           "Borderless Fullscreen"};

}

PanelResult drawSettingsPanel(const core::Rect& viewport, float slide, Settings& working,
                              bool saveFailed)
{
    if (slide > 0.f && slide < 1.f)
        ui::requestFrame();

    const float width = ui::px(420.f);
    const core::Rect panel(viewport.r() - width * slide, viewport.y, width, viewport.h);

    ui::dl().rect(panel, ui::theme().panelFill.alpha(1.f), 0.f);
    ui::dl().line(core::Vec2(panel.x, panel.y), core::Vec2(panel.x, panel.b()), ui::px(1.f), gold);

    const float inset = ui::px(24.f);
    const core::Rect title(panel.x + inset, panel.y + inset, panel.w - inset * 2.f, ui::px(28.f));
    ui::text(ui::fonts().title, title, "Settings", ui::theme().text, ui::AlignH::Left,
             ui::AlignV::Middle, ui::px(2.f));

    const float rowW = panel.w - inset * 2.f;
    const float rowH = ui::px(34.f);
    const float gap = ui::px(10.f);
    const float noteGap = ui::px(12.f);
    const float noteH = ui::px(14.f);
    constexpr std::string_view recheckNote = "CHANGING THIS RE-CHECKS THE INSTALL";

    int graphicsApiIndex = static_cast<int>(working.graphicsApi);
    int gpuPreferenceIndex = static_cast<int>(working.gpuPreference);
    int windowModeIndex = static_cast<int>(working.windowMode);
    int languageIndex = languageIndexFromCode(working.language);
    int audioLanguageIndex = audioLanguageIndexFromCode(working.audioLanguage);
    const int originalGraphicsApiIndex = graphicsApiIndex;
    const int originalGpuPreferenceIndex = gpuPreferenceIndex;
    const int originalWindowModeIndex = windowModeIndex;
    const int originalLanguageIndex = languageIndex;
    const int originalAudioLanguageIndex = audioLanguageIndex;

    float y = title.b() + gap;

    const core::Rect graphicsApiRow(panel.x + inset, y, rowW, rowH);
    dropdown(DropdownGroup::Settings, "settings.graphicsApi", graphicsApiRow, "Graphics API",
             graphicsApiNames, graphicsApiIndex);
    const core::Rect graphicsApiNote(graphicsApiRow.x, graphicsApiRow.b() + noteGap, rowW, noteH);
    ui::text(ui::fonts().caption, graphicsApiNote, recheckNote, ui::theme().subtext,
             ui::AlignH::Left, ui::AlignV::Middle, ui::px(1.f));
    y = graphicsApiNote.b() + gap;

    const core::Rect gpuPreferenceRow(panel.x + inset, y, rowW, rowH);
    dropdown(DropdownGroup::Settings, "settings.gpuPreference", gpuPreferenceRow, "GPU Preference",
             gpuPreferenceNames, gpuPreferenceIndex);
    y = gpuPreferenceRow.b() + gap;

    const core::Rect windowModeRow(panel.x + inset, y, rowW, rowH);
    dropdown(DropdownGroup::Settings, "settings.windowMode", windowModeRow, "Window Mode",
             windowModeNames, windowModeIndex);
    y = windowModeRow.b() + gap;

    const core::Rect languageRow(panel.x + inset, y, rowW, rowH);
    dropdown(DropdownGroup::Settings, "settings.language", languageRow, "Language",
             languageNames(), languageIndex);
    const core::Rect languageNote(languageRow.x, languageRow.b() + noteGap, rowW, noteH);
    ui::text(ui::fonts().caption, languageNote, recheckNote, ui::theme().subtext, ui::AlignH::Left,
             ui::AlignV::Middle, ui::px(1.f));
    y = languageNote.b() + gap;

    const core::Rect audioLanguageRow(panel.x + inset, y, rowW, rowH);
    dropdown(DropdownGroup::Settings, "settings.audioLanguage", audioLanguageRow, "Audio Language",
             audioLanguageNames(), audioLanguageIndex);
    y = audioLanguageRow.b() + gap;

    const core::Rect shaderCacheRow(panel.x + inset, y, rowW, rowH);
    checkbox("settings.shaderCache", shaderCacheRow, "Shader Cache", working.shaderCache);
    y = shaderCacheRow.b() + gap;

    const core::Rect bulkDownloadRow(panel.x + inset, y, rowW, rowH);
    checkbox("settings.bulkDownload", bulkDownloadRow, "Bulk Download", working.bulkDownload);
    y = bulkDownloadRow.b() + gap;

    const core::Rect aggressiveDownloadRow(panel.x + inset, y, rowW, rowH);
    checkbox("settings.aggressiveDownload", aggressiveDownloadRow, "Aggressive Download",
             working.aggressiveDownload);
    y = aggressiveDownloadRow.b() + gap;

    const core::Rect launcherGpuRow(panel.x + inset, y, rowW, rowH);
    checkbox("settings.launcherGpu", launcherGpuRow, "Launcher GPU Acceleration",
             working.launcherGpu);
    y = launcherGpuRow.b() + gap;

    // the ForceHTTPS inversion lives only in Settings::save(), never here
    const core::Rect allowNetworkCachesRow(panel.x + inset, y, rowW, rowH);
    checkbox("settings.allowNetworkCaches", allowNetworkCachesRow, "Allow Network Caches",
             working.allowNetworkCaches);
    y = allowNetworkCachesRow.b() + gap;

    dropdownOverlay(DropdownGroup::Settings);

    if (graphicsApiIndex != originalGraphicsApiIndex)
        working.graphicsApi = static_cast<GraphicsApi>(graphicsApiIndex);
    if (gpuPreferenceIndex != originalGpuPreferenceIndex)
        working.gpuPreference = static_cast<GpuPreference>(gpuPreferenceIndex);
    if (windowModeIndex != originalWindowModeIndex)
        working.windowMode = static_cast<WindowMode>(windowModeIndex);
    if (languageIndex != originalLanguageIndex)
        working.language = std::wstring(languageCodeFromIndex(languageIndex));
    if (audioLanguageIndex != originalAudioLanguageIndex)
        working.audioLanguage = std::wstring(audioLanguageCodeFromIndex(audioLanguageIndex));

    const float btnW = ui::px(96.f);
    const float btnH = ui::px(32.f);
    const float btnGap = ui::px(12.f);
    const core::Rect cancelBox(panel.x + panel.w - inset - btnW, panel.y + panel.h - inset - btnH,
                               btnW, btnH);
    const core::Rect okBox(cancelBox.x - btnGap - btnW, cancelBox.y, btnW, btnH);

    if (saveFailed)
    {
        const core::Rect failRect(panel.x + inset, okBox.y, okBox.x - (panel.x + inset), okBox.h);
        ui::text(ui::fonts().caption, failRect, "COULD NOT WRITE SETTINGS", ui::theme().fail,
                 ui::AlignH::Right, ui::AlignV::Middle, ui::px(1.f));
    }

    const bool okClicked = panelButton("settings.ok", okBox, "OK");
    const bool cancelClicked = panelButton("settings.cancel", cancelBox, "CANCEL");

    if (okClicked)
        return PanelResult::Accepted;
    if (cancelClicked)
        return PanelResult::Cancelled;
    return PanelResult::None;
}

}
