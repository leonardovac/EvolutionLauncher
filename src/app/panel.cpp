#include "app/panel.h"

#include "app/controls.h"
#include "app/icons.h"
#include "app/languages.h"
#include "app/theme.h"
#include "ui/ui.h"

#include <array>
#include <cstdint>
#include <ranges>
#include <string_view>

namespace app
{
namespace
{

constexpr std::array<std::string_view, 2> graphicsApiNames{"DirectX 11", "DirectX 12"};
constexpr std::array<std::string_view, 3> gpuPreferenceNames{"Let Windows Decide", "Power Saving",
                                                             "High Performance"};
constexpr std::array<std::string_view, 3> windowModeNames{"Windowed", "Fullscreen",
                                                          "Borderless Fullscreen"};

struct TabEntry
{
    PanelTab tab;
    std::string_view id;
    std::string_view label;
};

constexpr std::array<TabEntry, 3> tabEntries{
    {{PanelTab::Settings, "panel.tab.settings", "SETTINGS"},
     {PanelTab::Maintenance, "panel.tab.maintenance", "MAINTENANCE"},
     {PanelTab::Launcher, "panel.tab.launcher", "LAUNCHER"}}};

bool button(std::string_view id, const core::Rect& box, std::string_view label, ui::AlignH align)
{
    const std::uint32_t widget = ui::id(id);
    const bool hit = ui::clicked(widget, box);
    const bool hot = ui::hovered(widget, box);
    const float hoverT = ui::anim(widget, 0, hot ? 1.f : 0.f, 16.f);
    if (hoverT > 0.01f)
        ui::dl().rect(box, accent().alpha(0.12f * hoverT), ui::px(2.f));
    ui::dl().border(box, accent().alpha(0.5f + 0.5f * hoverT), ui::px(1.f), ui::px(2.f));
    const float pad = align == ui::AlignH::Left ? ui::px(12.f) : 0.f;
    const core::Rect caption(box.x + pad, box.y, box.w - pad * 2.f, box.h);
    ui::text(ui::fonts().caption, caption, label, accent().alpha(0.85f + 0.15f * hoverT), align,
             ui::AlignV::Middle, ui::px(2.f));
    return hit;
}

void note(const core::Rect& box, std::string_view text)
{
    ui::text(ui::fonts().caption, box, text, ui::theme().subtext, ui::AlignH::Left,
             ui::AlignV::Middle, ui::px(1.f));
}

// sits after a checkbox label to mark a setting that rewrites a game file
void warnMark(const core::Rect& row, std::string_view label)
{
    const float x = row.x + ui::px(26.f) + trackedWidth(ui::fonts().caption, label, ui::px(1.f))
                  + ui::px(10.f);
    const core::Rect box(x, row.y, ui::px(16.f), row.h);
    const core::Col col = ui::theme().warn;
    if (iconsReady())
    {
        drawIcon(IconSize::Caption, icon::warning, box, col);
        return;
    }
    const core::Vec2 c = box.center();
    const float half = ui::px(6.f);
    ui::dl().line(core::Vec2(c.x, c.y - half), core::Vec2(c.x - half, c.y + half), ui::px(1.2f),
                  col);
    ui::dl().line(core::Vec2(c.x, c.y - half), core::Vec2(c.x + half, c.y + half), ui::px(1.2f),
                  col);
    ui::dl().line(core::Vec2(c.x - half, c.y + half), core::Vec2(c.x + half, c.y + half),
                  ui::px(1.2f), col);
}

}

PanelAction drawPanel(const core::Rect& viewport, float slide, PanelState& state,
                      Settings& working)
{
    if (slide > 0.f && slide < 1.f)
        ui::requestFrame();

    const float width = ui::px(420.f);
    const core::Rect panel(viewport.r() - width * slide, viewport.y, width, viewport.h);

    // the shell already drew the hero across the whole viewport, so a wash over it is the
    // backdrop: no redraw, no clip, and the art lines up exactly with what the panel covers
    const core::Col base = core::Col::hex(0x0B0A0A, 1.f);
    // cast onto the art so the panel reads as a layer above it rather than a cut-out
    ui::dl().shadow(panel, base.alpha(0.55f), ui::px(30.f), 0.f, core::Vec2(-ui::px(6.f), 0.f));
    // uniform: a softened edge reads as a stray line against the border, not as depth
    ui::dl().rect(panel, base.alpha(0.86f), 0.f);
    ui::dl().line(core::Vec2(panel.x, panel.y), core::Vec2(panel.x, panel.b()), ui::px(1.f),
                  accent());

    const float inset = ui::px(24.f);
    const float rowW = panel.w - inset * 2.f;
    const float rowH = ui::px(34.f);
    const float gap = ui::px(10.f);
    const float noteGap = ui::px(12.f);
    const float noteH = ui::px(14.f);

    const float tabTracking = ui::px(2.f);
    // clears the window controls, which share the panel's top edge
    const float stripY = panel.y + ui::px(56.f);
    const float ruleY = stripY + ui::px(26.f);
    ui::dl().line(core::Vec2(panel.x + inset, ruleY), core::Vec2(panel.r() - inset, ruleY),
                  ui::px(1.f), accent().alpha(0.22f));

    float tabX = panel.x + inset;
    for (const TabEntry& entry : tabEntries)
    {
        const float tabW =
            trackedWidth(ui::fonts().caption, entry.label, tabTracking) + ui::px(14.f) * 2.f;
        const core::Rect box(tabX, stripY, tabW, ui::px(26.f));
        const std::uint32_t widget = ui::id(entry.id);
        const bool hot = ui::hovered(widget, box);
        if (ui::clicked(widget, box))
            state.tab = entry.tab;
        const bool active = state.tab == entry.tab;
        const float mark = ui::anim(widget, 0, active ? 1.f : 0.f, 16.f);
        ui::text(ui::fonts().caption, box, entry.label,
                 active ? accent() : ui::theme().text.alpha(hot ? 0.95f : 0.62f),
                 ui::AlignH::Center,
                 ui::AlignV::Middle, tabTracking);
        if (mark > 0.01f)
        {
            const float half = (tabW - ui::px(14.f)) * 0.5f * mark;
            ui::dl().rect(
                core::Rect(box.center().x - half, ruleY - ui::px(1.f), half * 2.f, ui::px(2.f)),
                accent().alpha(mark), ui::px(1.f));
        }
        tabX = box.r() + ui::px(4.f);
    }

    PanelAction action = PanelAction::None;
    float y = ruleY + ui::px(20.f);

    if (state.tab == PanelTab::Settings)
    {
        int graphicsApiIndex = static_cast<int>(working.graphicsApi);
        int gpuPreferenceIndex = static_cast<int>(working.gpuPreference);
        int windowModeIndex = static_cast<int>(working.windowMode);
        int audioLanguageIndex = audioLanguageIndexFromCode(working.audioLanguage);
        const int originalGraphicsApiIndex = graphicsApiIndex;
        const int originalGpuPreferenceIndex = gpuPreferenceIndex;
        const int originalWindowModeIndex = windowModeIndex;
        const int originalAudioLanguageIndex = audioLanguageIndex;

        const core::Rect graphicsApiRow(panel.x + inset, y, rowW, rowH);
        dropdown(DropdownGroup::Settings, "settings.graphicsApi", graphicsApiRow, "Graphics API",
                 graphicsApiNames, graphicsApiIndex);
        y = graphicsApiRow.b() + gap;

        const core::Rect gpuPreferenceRow(panel.x + inset, y, rowW, rowH);
        dropdown(DropdownGroup::Settings, "settings.gpuPreference", gpuPreferenceRow,
                 "GPU Preference", gpuPreferenceNames, gpuPreferenceIndex);
        y = gpuPreferenceRow.b() + gap;

        const core::Rect windowModeRow(panel.x + inset, y, rowW, rowH);
        dropdown(DropdownGroup::Settings, "settings.windowMode", windowModeRow, "Window Mode",
                 windowModeNames, windowModeIndex);
        y = windowModeRow.b() + gap;

        // the header chip owns the interface language; a second control for it would only
        // be a place for the two to disagree
        const core::Rect audioLanguageRow(panel.x + inset, y, rowW, rowH);
        dropdown(DropdownGroup::Settings, "settings.audioLanguage", audioLanguageRow,
                 "Audio Language", audioLanguageNames(), audioLanguageIndex);
        y = audioLanguageRow.b() + gap;

        const core::Rect shaderCacheRow(panel.x + inset, y, rowW, rowH);
        checkbox("settings.shaderCache", shaderCacheRow, "Shader Cache", working.shaderCache);
        y = shaderCacheRow.b() + gap;

        constexpr std::string_view sideloadLabel = "Allow Sideloading DLLs";
        const core::Rect sideloadRow(panel.x + inset, y, rowW, rowH);
        checkbox("settings.sideload", sideloadRow, sideloadLabel, working.sideload);
        warnMark(sideloadRow, sideloadLabel);
        tooltip(sideloadRow, "PATCHES THE GAME EXE. CHANGING THIS RE-DOWNLOADS IT.");
        y = sideloadRow.b() + gap;

        // one quiet footnote instead of the same warning repeated against each row
        const core::Rect recheck(panel.x + inset, sideloadRow.b() + ui::px(18.f), rowW, noteH);
        ui::text(ui::fonts().caption, recheck, "SOME SETTINGS RE-CHECK THE INSTALL",
                 ui::theme().subtext.alpha(0.7f), ui::AlignH::Left, ui::AlignV::Middle,
                 ui::px(1.f));

        dropdownOverlay(DropdownGroup::Settings);

        if (graphicsApiIndex != originalGraphicsApiIndex)
            working.graphicsApi = static_cast<GraphicsApi>(graphicsApiIndex);
        if (gpuPreferenceIndex != originalGpuPreferenceIndex)
            working.gpuPreference = static_cast<GpuPreference>(gpuPreferenceIndex);
        if (windowModeIndex != originalWindowModeIndex)
            working.windowMode = static_cast<WindowMode>(windowModeIndex);
        if (audioLanguageIndex != originalAudioLanguageIndex)
            working.audioLanguage = std::wstring(audioLanguageCodeFromIndex(audioLanguageIndex));
    }
    else if (state.tab == PanelTab::Launcher)
    {
        // the ForceHTTPS inversion lives only in Settings::save(), never here
        const core::Rect allowNetworkCachesRow(panel.x + inset, y, rowW, rowH);
        checkbox("launcher.allowNetworkCaches", allowNetworkCachesRow, "Allow Network Caches",
                 working.allowNetworkCaches);
        tooltip(allowNetworkCachesRow, "SHARED BY BOTH GAMES");
    }
    else
    {
        const core::Rect verifyBox(panel.x + inset, y, rowW, rowH);
        if (button("panel.verify", verifyBox, "VERIFY THE INSTALL", ui::AlignH::Left))
            action = PanelAction::Verify;
        tooltip(verifyBox, "READS EVERY FILE, CACHE INCLUDED. SLOW.");
        y = verifyBox.b() + gap;

        const core::Rect staleBox(panel.x + inset, y, rowW, rowH);
        if (state.staleRunning)
        {
            ui::text(ui::fonts().caption, staleBox, "SCANNING...", ui::theme().text,
                     ui::AlignH::Left, ui::AlignV::Middle, ui::px(1.f));
            ui::requestFrame();
        }
        else if (button("panel.stale", staleBox, "FIND UNLISTED FILES", ui::AlignH::Left))
        {
            action = PanelAction::StaleReport;
        }
        tooltip(staleBox, "NOTHING IS REMOVED");
        y = staleBox.b() + gap;
        // the scan writes its result here, so this line stays put rather than hiding on hover
        if (!state.staleLine.empty())
        {
            const core::Rect staleNote(staleBox.x, staleBox.b() + noteGap, rowW, noteH);
            note(staleNote, state.staleLine);
            y = staleNote.b() + ui::px(18.f);
        }

        const core::Rect defragBox(panel.x + inset, y, rowW, rowH);
        if (button("panel.defrag", defragBox, "DEFRAGMENT THE CACHE", ui::AlignH::Left))
            action = PanelAction::Defragment;
        tooltip(defragBox, "RUNS THE GAME'S OWN DEFRAGMENTER");
        y = defragBox.b() + gap;

        if (!state.defragLine.empty())
        {
            const core::Rect failure(panel.x + inset, y, rowW, noteH);
            ui::text(ui::fonts().caption, failure, state.defragLine, ui::theme().fail,
                     ui::AlignH::Left, ui::AlignV::Middle, ui::px(1.f));
        }
    }

    const float btnW = ui::px(96.f);
    const float btnH = ui::px(32.f);
    const float btnGap = ui::px(12.f);
    const core::Rect lastBox(panel.r() - inset - btnW, panel.b() - inset - btnH, btnW, btnH);

    // versions are reference, not a destination; they sit in view instead of behind a tab
    const core::Rect launcherLine(panel.x + inset, lastBox.y, rowW, noteH);
    note(launcherLine, state.launcherLine);
    if (!state.gameBuildLine.empty())
        note(core::Rect(panel.x + inset, lastBox.y + noteH + ui::px(2.f), rowW, noteH),
             state.gameBuildLine);

    constexpr std::array editingTabs{PanelTab::Settings, PanelTab::Launcher};
    if (std::ranges::contains(editingTabs, state.tab))
    {
        const core::Rect okBox(lastBox.x - btnGap - btnW, lastBox.y, btnW, btnH);
        if (state.saveFailed)
        {
            const core::Rect failRect(panel.x + inset, okBox.y - noteH - ui::px(6.f),
                                      okBox.x - (panel.x + inset), okBox.h);
            ui::text(ui::fonts().caption, failRect, "COULD NOT WRITE SETTINGS", ui::theme().fail,
                     ui::AlignH::Right, ui::AlignV::Middle, ui::px(1.f));
        }
        if (button("panel.ok", okBox, "OK", ui::AlignH::Center))
            action = PanelAction::Accept;
        if (button("panel.cancel", lastBox, "CANCEL", ui::AlignH::Center))
            action = PanelAction::Dismiss;
    }
    else if (button("panel.close", lastBox, "CLOSE", ui::AlignH::Center))
    {
        action = PanelAction::Dismiss;
    }

    if (slide >= 1.f && ui::g().input.pressed && !panel.contains(ui::g().input.mouse))
        action = PanelAction::Dismiss;

    // last, so the hint sits above every row and above an open dropdown
    if (slide >= 1.f)
        tooltipOverlay(panel);

    return action;
}

}
