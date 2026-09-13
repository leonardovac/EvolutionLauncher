#include "app/railmenu.h"

#include "ui/ui.h"

#include <array>
#include <cstdint>
#include <string_view>

namespace app
{
namespace
{

const core::Col gold = core::Col::hex(0xD9C07A, 1.f);

bool menuRow(std::string_view id, const core::Rect& box, std::string_view label)
{
    const std::uint32_t widget = ui::id(id);
    const bool hit = ui::clicked(widget, box);
    const bool hot = ui::hovered(widget, box);
    if (hot)
        ui::dl().rect(box, gold.alpha(0.10f), ui::px(2.f));
    ui::dl().border(box, hot ? gold : gold.alpha(0.45f), ui::px(1.f), ui::px(2.f));
    const core::Rect caption(box.x + ui::px(12.f), box.y, box.w - ui::px(12.f), box.h);
    ui::text(ui::fonts().caption, caption, label, hot ? gold : ui::theme().text, ui::AlignH::Left,
             ui::AlignV::Middle, ui::px(2.f));
    return hit;
}

struct Row
{
    std::string_view id;
    std::string_view label;
    MenuAction action;
};

constexpr std::array<Row, 4> rows{{{"menu.settings", "SETTINGS", MenuAction::Settings},
                                   {"menu.verify", "VERIFY", MenuAction::Verify},
                                   {"menu.versions", "VERSIONS", MenuAction::Versions},
                                   {"menu.optimize", "OPTIMIZE", MenuAction::Optimize}}};

}

MenuAction drawRailMenu(const core::Rect& viewport, float slide, const MenuState& state)
{
    if (slide > 0.f && slide < 1.f)
        ui::requestFrame();

    const float width = ui::px(420.f);
    const core::Rect panel(viewport.r() - width * slide, viewport.y, width, viewport.h);
    ui::dl().rect(panel, ui::theme().panelFill.alpha(1.f), 0.f);
    ui::dl().line(core::Vec2(panel.x, panel.y), core::Vec2(panel.x, panel.b()), ui::px(1.f), gold);

    const float inset = ui::px(24.f);
    const float rowW = panel.w - inset * 2.f;
    const float rowH = ui::px(34.f);
    const float gap = ui::px(10.f);

    const core::Rect title(panel.x + inset, panel.y + inset, rowW, ui::px(28.f));
    float y = title.b() + gap;
    MenuAction action = MenuAction::None;

    if (state.view == MenuView::Rows)
    {
        ui::text(ui::fonts().title, title, "Launcher", ui::theme().text, ui::AlignH::Left,
                 ui::AlignV::Middle, ui::px(2.f));
        for (const Row& row : rows)
        {
            const core::Rect box(panel.x + inset, y, rowW, rowH);
            if (menuRow(row.id, box, row.label))
                action = row.action;
            y = box.b() + gap;
        }
    }
    else if (state.view == MenuView::Versions)
    {
        ui::text(ui::fonts().title, title, "Versions", ui::theme().text, ui::AlignH::Left,
                 ui::AlignV::Middle, ui::px(2.f));
        const core::Rect launcher(panel.x + inset, y, rowW, rowH);
        ui::text(ui::fonts().caption, launcher, state.launcherLine, ui::theme().text,
                 ui::AlignH::Left, ui::AlignV::Middle, ui::px(1.f));
        y = launcher.b() + gap;
        if (!state.engineLine.empty())
        {
            const core::Rect engine(panel.x + inset, y, rowW, rowH);
            ui::text(ui::fonts().caption, engine, state.engineLine, ui::theme().text,
                     ui::AlignH::Left, ui::AlignV::Middle, ui::px(1.f));
            y = engine.b() + gap;
        }
    }
    else
    {
        ui::text(ui::fonts().title, title, "Optimize", ui::theme().text, ui::AlignH::Left,
                 ui::AlignV::Middle, ui::px(2.f));
        const core::Rect line(panel.x + inset, y, rowW, rowH);
        ui::text(ui::fonts().caption, line,
                 state.optimizeRunning ? "WALKING THE INSTALL…" : state.optimizeLine,
                 ui::theme().text, ui::AlignH::Left, ui::AlignV::Middle, ui::px(1.f));
        y = line.b() + gap;
        const core::Rect note(panel.x + inset, y, rowW, ui::px(14.f));
        ui::text(ui::fonts().caption, note, "NONE OF IT IS REMOVED", ui::theme().subtext,
                 ui::AlignH::Left, ui::AlignV::Middle, ui::px(1.f));
        y = note.b() + gap;
        if (!state.optimizeRunning)
        {
            const core::Rect defragBox(panel.x + inset, y, rowW, rowH);
            if (menuRow("menu.defrag", defragBox, "DEFRAGMENT THE CACHE"))
                action = MenuAction::Defragment;
            y = defragBox.b() + gap;
            const core::Rect caution(panel.x + inset, y, rowW, ui::px(14.f));
            ui::text(ui::fonts().caption, caution, "RUNS THE GAME'S OWN DEFRAGMENTER",
                     ui::theme().subtext, ui::AlignH::Left, ui::AlignV::Middle, ui::px(1.f));
            y = caution.b() + gap;
        }
        if (!state.defragLine.empty())
        {
            const core::Rect failure(panel.x + inset, y, rowW, ui::px(14.f));
            ui::text(ui::fonts().caption, failure, state.defragLine, ui::theme().fail,
                     ui::AlignH::Left, ui::AlignV::Middle, ui::px(1.f));
            y = failure.b() + gap;
        }
        if (state.optimizeRunning)
            ui::requestFrame();
    }

    const float btnW = ui::px(96.f);
    const float btnH = ui::px(32.f);
    const core::Rect closeBox(panel.x + panel.w - inset - btnW, panel.y + panel.h - inset - btnH,
                              btnW, btnH);
    const bool backView = state.view != MenuView::Rows;
    if (menuRow("menu.close", closeBox, backView ? "BACK" : "CLOSE"))
        action = backView ? MenuAction::Back : MenuAction::Dismiss;

    return action;
}

}
