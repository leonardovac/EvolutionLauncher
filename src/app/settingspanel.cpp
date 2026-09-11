#include "app/settingspanel.h"

#include "ui/ui.h"

#include <cstdint>

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

}

PanelResult drawSettingsPanel(const core::Rect& viewport, float slide, Settings& working)
{
    (void)working;

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

    const float btnW = ui::px(96.f);
    const float btnH = ui::px(32.f);
    const float gap = ui::px(12.f);
    const core::Rect cancelBox(panel.x + panel.w - inset - btnW, panel.y + panel.h - inset - btnH,
                               btnW, btnH);
    const core::Rect okBox(cancelBox.x - gap - btnW, cancelBox.y, btnW, btnH);

    const bool okClicked = panelButton("settings.ok", okBox, "OK");
    const bool cancelClicked = panelButton("settings.cancel", cancelBox, "CANCEL");

    if (okClicked)
        return PanelResult::Accepted;
    if (cancelClicked)
        return PanelResult::Cancelled;
    return PanelResult::None;
}

}
