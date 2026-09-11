#include "app/shell.h"

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
bool gearClicked = false;

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

    const Rect frame(viewport.x + inset, viewport.y + inset, viewport.w - inset * 2.f,
                     viewport.h - inset * 2.f);
    ui::dl().border(frame, gold.alpha(0.45f), ui::px(1.f), 0.f);

    const Rect title(frame.x + ui::px(24.f), frame.y + ui::px(18.f), ui::px(260.f), ui::px(28.f));
    ui::text(ui::fonts().title, title, "WARFRAME", gold, ui::AlignH::Left, ui::AlignV::Middle,
             ui::px(4.f));

    const float chipW = ui::px(96.f);
    const float chipH = ui::px(26.f);
    const Rect chip(frame.x + frame.w - ui::px(150.f), frame.y + ui::px(18.f), chipW, chipH);
    ui::dl().border(chip, gold.alpha(0.5f), ui::px(1.f), 0.f);
    ui::text(ui::fonts().caption, chip, "LANGUAGE  EN", gold, ui::AlignH::Center,
             ui::AlignV::Middle, ui::px(1.f));

    const Rect gearBox(frame.x + frame.w - ui::px(64.f), frame.y + ui::px(16.f), ui::px(22.f),
                       ui::px(22.f));
    const std::uint32_t gearId = ui::id("shell.gear");
    const bool gearHot = ui::hovered(gearId, gearBox);
    gearClicked = ui::clicked(gearId, gearBox) && !state.panelVisible;
    const Col gearCol = gold.alpha(gearHot ? 1.f : 0.7f);
    const Vec2 gearCenter = gearBox.center();
    const float gearRadius = gearBox.w * 0.3f;
    ui::dl().arc(gearCenter, gearRadius, ui::px(1.5f), 0.f, core::kPi * 2.f, gearCol);
    for (int spoke = 0; spoke < 6; ++spoke)
    {
        const float angle = core::kPi * 2.f * static_cast<float>(spoke) / 6.f;
        const Vec2 dir(std::cos(angle), std::sin(angle));
        const Vec2 inner(gearCenter.x + dir.x * gearRadius, gearCenter.y + dir.y * gearRadius);
        const Vec2 outer(gearCenter.x + dir.x * (gearRadius + ui::px(4.f)),
                         gearCenter.y + dir.y * (gearRadius + ui::px(4.f)));
        ui::dl().line(inner, outer, ui::px(1.5f), gearCol);
    }

    const Rect closeBox(frame.x + frame.w - ui::px(34.f), frame.y + ui::px(16.f), ui::px(22.f),
                        ui::px(22.f));
    closeClicked = ui::closeButton("shell.close", closeBox) && !state.panelVisible;

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
    startClicked = state.startEnabled && ui::clicked(startId, start) && !state.panelVisible;
    const bool hot = state.startEnabled && ui::hovered(startId, start);
    ui::dl().border(start, hot ? startCol : startCol.alpha(0.8f), ui::px(1.f), 0.f);
    ui::text(ui::fonts().title, start, "GAME START", startCol, ui::AlignH::Center,
             ui::AlignV::Middle, ui::px(5.f));
}

bool shellCloseClicked()
{
    return closeClicked;
}

bool shellStartClicked()
{
    return startClicked;
}

bool shellGearClicked()
{
    return gearClicked;
}

}
