#include "app/shell.h"

#include "app/controls.h"
#include "app/languages.h"
#include "app/rail.h"
#include "ui/ui.h"
#include "ui/widgets.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <ranges>
#include <string>

namespace app
{
namespace
{

bool closeClicked = false;
bool startClicked = false;
bool minimiseClicked = false;
int languageIndex = -1;

void globeGlyph(const core::Vec2& center, float radius, const core::Col& col)
{
    ui::dl().arc(center, radius, ui::px(1.f), 0.f, core::kPi * 2.f, col);
    ui::dl().line(core::Vec2(center.x - radius, center.y), core::Vec2(center.x + radius, center.y),
                  ui::px(1.f), col);
    ui::dl().arc(core::Vec2(center.x, center.y), radius * 0.5f, ui::px(1.f), 0.f, core::kPi * 2.f,
                 col);
}

}

void drawShell(const core::Rect& viewport, gfx::Image* hero, const ShellState& state)
{
    using core::Col;
    using core::Rect;
    using core::Vec2;

    const Col gold = Col::hex(0xD9C07A, 1.f);
    const float inset = ui::px(28.f);

    ui::dl().rect(viewport, ui::theme().body);
    ui::heroCard(viewport, hero, ui::theme().focus, 0.f, 1.f);
    ui::heroOverlay(viewport, 0.f, 0.35f);

    const Rect content(viewport.x + ui::px(railWidth), viewport.y,
                       viewport.w - ui::px(railWidth), viewport.h);
    const Rect frame(content.x + inset, content.y + inset, content.w - inset * 2.f,
                     content.h - inset * 2.f);
    ui::dl().border(frame, gold.alpha(0.45f), ui::px(1.f), 0.f);

    const Rect title(frame.x + ui::px(24.f), frame.y + ui::px(18.f), ui::px(260.f), ui::px(28.f));
    ui::text(ui::fonts().title, title, "WARFRAME", gold, ui::AlignH::Left, ui::AlignV::Middle,
             ui::px(4.f));

    const float glyphSize = ui::px(22.f);
    const float glyphTop = frame.y + ui::px(16.f);

    const Rect closeBox(frame.x + frame.w - ui::px(28.f) - glyphSize, glyphTop, glyphSize,
                        glyphSize);
    closeClicked = ui::closeButton("shell.close", closeBox) && !state.panelVisible;

    const Rect minimiseBox(closeBox.x - ui::px(34.f), glyphTop, glyphSize, glyphSize);
    const std::uint32_t minimiseId = ui::id("shell.minimise");
    const bool minimiseHot = ui::hovered(minimiseId, minimiseBox);
    const bool minimiseHit = ui::clicked(minimiseId, minimiseBox);
    minimiseClicked = minimiseHit && !state.panelVisible;
    const Vec2 minimiseCenter = minimiseBox.center();
    ui::dl().line(Vec2(minimiseCenter.x - ui::px(6.f), minimiseCenter.y),
                  Vec2(minimiseCenter.x + ui::px(6.f), minimiseCenter.y), ui::px(1.5f),
                  gold.alpha(minimiseHot ? 1.f : 0.7f));

    const float dividerX = minimiseBox.x - ui::px(18.f);
    ui::dl().line(Vec2(dividerX, minimiseCenter.y - ui::px(7.f)),
                  Vec2(dividerX, minimiseCenter.y + ui::px(7.f)), ui::px(1.f), gold.alpha(0.3f));

    const float languageW = ui::px(96.f);
    const Rect languageRow(dividerX - ui::px(16.f) - languageW, glyphTop + ui::px(-2.f), languageW,
                           ui::px(26.f));
    int index = state.languageIndex;
    const int before = index;
    dropdown(DropdownGroup::Shell, "shell.language", languageRow, "", languageNames(), index,
             languageShortLabel(index));
    globeGlyph(Vec2(languageRow.x + ui::px(11.f), languageRow.center().y), ui::px(7.f),
               gold.alpha(0.8f));

    const Rect bottom(frame.x + ui::px(24.f), frame.y + frame.h - ui::px(78.f),
                      frame.w - ui::px(48.f), ui::px(54.f));

    constexpr std::array sweepPhases{JobPhase::Idle, JobPhase::Checking};
    constexpr std::array barPhases{JobPhase::Idle, JobPhase::Checking, JobPhase::Updating};
    const bool showBar = std::ranges::contains(barPhases, state.phase);
    if (showBar)
    {
        const Rect label(bottom.x, bottom.y, bottom.w * 0.5f, ui::px(16.f));
        ui::text(ui::fonts().caption, label, state.statusLine, ui::theme().text, ui::AlignH::Left,
                 ui::AlignV::Middle, ui::px(1.f));
        const Rect track(bottom.x, bottom.y + ui::px(22.f), bottom.w * 0.62f, ui::px(3.f));
        if (std::ranges::contains(sweepPhases, state.phase))
        {
            ui::dl().rect(track, Col::hex(0xFFFFFF, 0.07f), track.h * 0.5f);
            const float sweep = 0.5f + 0.5f * std::sin(ui::g().time * 2.2f);
            const float head = track.w * 0.22f;
            const Rect segment(track.x + (track.w - head) * sweep, track.y, head, track.h);
            ui::dl().rect(segment, gold.alpha(0.85f), track.h * 0.5f);
            ui::requestFrame();
        }
        else
        {
            ui::loadingLine(track, state.progress, ui::g().time, gold, 1.f);
        }
        if (!state.fileLine.empty())
        {
            const Rect file(bottom.x, bottom.y + ui::px(32.f), bottom.w * 0.62f, ui::px(14.f));
            ui::text(ui::fonts().caption, file, state.fileLine, ui::theme().subtext,
                     ui::AlignH::Left, ui::AlignV::Middle, ui::px(1.f));
        }
    }
    else
    {
        const Col line = state.phase == JobPhase::Failed ? ui::theme().fail : ui::theme().subtext;
        const Rect label(bottom.x, bottom.y + ui::px(14.f), bottom.w * 0.6f, ui::px(16.f));
        const std::string_view text =
            state.phase == JobPhase::Ready ? state.buildLabel : state.statusLine;
        ui::text(ui::fonts().caption, label, text, line, ui::AlignH::Left, ui::AlignV::Middle,
                 ui::px(1.f));
    }

    const Rect start(bottom.x + bottom.w - ui::px(240.f), bottom.y, ui::px(240.f), ui::px(44.f));
    const Col startCol = state.startEnabled ? gold : gold.alpha(0.35f);
    const std::uint32_t startId = ui::id("shell.start");
    const bool startHit = ui::clicked(startId, start);
    startClicked = state.startEnabled && startHit && !state.panelVisible;
    const bool hot = state.startEnabled && ui::hovered(startId, start);
    ui::dl().border(start, hot ? startCol : startCol.alpha(0.8f), ui::px(1.f), 0.f);
    ui::text(ui::fonts().title, start, "GAME START", startCol, ui::AlignH::Center,
             ui::AlignV::Middle, ui::px(5.f));

    dropdownOverlay(DropdownGroup::Shell);
    languageIndex = index != before ? index : -1;
}

bool shellCloseClicked()
{
    return closeClicked;
}

bool shellStartClicked()
{
    return startClicked;
}

bool shellMinimiseClicked()
{
    return minimiseClicked;
}

int shellLanguageIndex()
{
    return languageIndex;
}

}
