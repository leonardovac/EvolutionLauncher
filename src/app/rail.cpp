#include "app/rail.h"

#include "ui/ui.h"

#include <cmath>
#include <cstdint>

namespace app
{
namespace
{

const core::Col gold = core::Col::hex(0xD9C07A, 1.f);

void diamond(const core::Vec2& center, float radius, float thickness, const core::Col& col)
{
    const core::Vec2 top(center.x, center.y - radius);
    const core::Vec2 right(center.x + radius, center.y);
    const core::Vec2 bottom(center.x, center.y + radius);
    const core::Vec2 left(center.x - radius, center.y);
    ui::dl().line(top, right, thickness, col);
    ui::dl().line(right, bottom, thickness, col);
    ui::dl().line(bottom, left, thickness, col);
    ui::dl().line(left, top, thickness, col);
}

void gameGlyph(const core::Vec2& center, float size, const core::Col& col)
{
    const float half = size * 0.5f;
    const core::Vec2 apex(center.x, center.y - half);
    const core::Vec2 baseLeft(center.x - half, center.y + half);
    const core::Vec2 baseRight(center.x + half, center.y + half);
    ui::dl().line(apex, baseLeft, ui::px(1.5f), col);
    ui::dl().line(apex, baseRight, ui::px(1.5f), col);
    ui::dl().line(core::Vec2(center.x - half * 0.55f, center.y + half * 0.15f),
                  core::Vec2(center.x + half * 0.55f, center.y + half * 0.15f), ui::px(1.5f), col);
}

void cogGlyph(const core::Vec2& center, float radius, const core::Col& col)
{
    ui::dl().arc(center, radius, ui::px(1.5f), 0.f, core::kPi * 2.f, col);
    for (int spoke = 0; spoke < 6; ++spoke)
    {
        const float angle = core::kPi * 2.f * static_cast<float>(spoke) / 6.f;
        const core::Vec2 dir(std::cos(angle), std::sin(angle));
        const core::Vec2 inner(center.x + dir.x * radius, center.y + dir.y * radius);
        const core::Vec2 outer(center.x + dir.x * (radius + ui::px(4.f)),
                               center.y + dir.y * (radius + ui::px(4.f)));
        ui::dl().line(inner, outer, ui::px(1.5f), col);
    }
}

}

RailResult drawRail(const core::Rect& viewport, bool inputEnabled)
{
    RailResult result;

    const float width = ui::px(railWidth);
    const core::Rect rail(viewport.x, viewport.y, width, viewport.h);
    ui::dl().rect(rail, ui::theme().panelFill.alpha(0.92f), 0.f);
    ui::dl().line(core::Vec2(rail.r(), rail.y), core::Vec2(rail.r(), rail.b()), ui::px(1.f),
                  gold.alpha(0.25f));

    const float centerX = rail.x + rail.w * 0.5f;

    const core::Vec2 markCenter(centerX, rail.y + ui::px(39.f));
    diamond(markCenter, ui::px(13.f), ui::px(1.5f), gold.alpha(0.75f));
    diamond(markCenter, ui::px(5.f), ui::px(1.5f), gold.alpha(0.75f));

    const core::Rect band(rail.x, rail.y + ui::px(132.f), rail.w, ui::px(68.f));
    ui::dl().rect(band, gold.alpha(0.10f), 0.f);
    ui::dl().rect(core::Rect(rail.x, band.y, ui::px(3.f), band.h), gold, 0.f);
    gameGlyph(core::Vec2(centerX, band.y + ui::px(22.f)), ui::px(22.f), gold);
    const core::Rect gameLabel(band.x, band.b() - ui::px(22.f), band.w, ui::px(16.f));
    ui::text(ui::fonts().caption, gameLabel, "WARFRAME", gold, ui::AlignH::Center,
             ui::AlignV::Middle, ui::px(1.f));

    const float cogSize = ui::px(22.f);
    const core::Rect cogBox(centerX - cogSize * 0.5f, rail.b() - ui::px(26.f) - cogSize, cogSize,
                            cogSize);
    const std::uint32_t cogId = ui::id("rail.cog");
    const bool cogHot = ui::hovered(cogId, cogBox);
    const bool cogHit = ui::clicked(cogId, cogBox);
    result.cogClicked = cogHit && inputEnabled;
    cogGlyph(cogBox.center(), cogBox.w * 0.3f, gold.alpha(cogHot ? 1.f : 0.7f));

    return result;
}

}
