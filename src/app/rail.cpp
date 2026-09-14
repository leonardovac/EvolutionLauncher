#include "app/rail.h"

#include "app/icons.h"
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

void titleGlyph(const core::Vec2& center, float size, const core::Col& col)
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

// alpha-only art; kAlphaMask paints it flat in `col` instead of sampling its own pixels
void drawMark(gfx::Image* art, const core::Vec2& center, float size, const core::Col& col)
{
    const core::Rect box(center.x - size * 0.5f, center.y - size * 0.5f, size, size);
    ui::dl().image(art->srv.get(), box, col, 0.f, core::Rect(0.f, 0.f, 1.f, 1.f),
                   gfx::DrawList::kAlphaMask);
}

}

RailResult drawRail(const core::Rect& viewport, bool inputEnabled, gfx::Image* publisher,
                    std::span<const RailTitle> titles, int selected, float cogAxis)
{
    RailResult result;

    const float width = ui::px(railWidth);
    const core::Rect rail(viewport.x, viewport.y, width, viewport.h);
    ui::dl().rect(rail, ui::theme().panelFill.alpha(0.92f), 0.f);
    ui::dl().line(core::Vec2(rail.r(), rail.y), core::Vec2(rail.r(), rail.b()), ui::px(1.f),
                  gold.alpha(0.25f));

    const float centerX = rail.x + rail.w * 0.5f;

    const core::Vec2 markCenter(centerX, rail.y + ui::px(40.f));
    if (publisher != nullptr && publisher->valid())
        drawMark(publisher, markCenter, ui::px(40.f), ui::theme().text.alpha(0.9f));
    else
    {
        diamond(markCenter, ui::px(13.f), ui::px(1.5f), gold.alpha(0.75f));
        diamond(markCenter, ui::px(5.f), ui::px(1.5f), gold.alpha(0.75f));
    }

    // the rail is too narrow for the name on one line
    const core::Rect publisherTop(rail.x, rail.y + ui::px(64.f), rail.w, ui::px(13.f));
    const core::Rect publisherLower(rail.x, rail.y + ui::px(77.f), rail.w, ui::px(13.f));
    ui::text(ui::fonts().caption, publisherTop, "DIGITAL", ui::theme().subtext, ui::AlignH::Center,
             ui::AlignV::Middle, ui::px(1.4f));
    ui::text(ui::fonts().caption, publisherLower, "EXTREMES", ui::theme().subtext,
             ui::AlignH::Center, ui::AlignV::Middle, ui::px(1.4f));

    const float ruleY = rail.y + ui::px(104.f);
    ui::dl().line(core::Vec2(rail.x + ui::px(18.f), ruleY),
                  core::Vec2(rail.r() - ui::px(18.f), ruleY), ui::px(1.f), gold.alpha(0.22f));

    float bandY = rail.y + ui::px(120.f);
    for (std::size_t i = 0; i < titles.size(); ++i)
    {
        const RailTitle& entry = titles[i];
        const core::Rect band(rail.x, bandY, rail.w, ui::px(68.f));
        const std::uint32_t widget = ui::id(entry.id);
        const bool hot = ui::hovered(widget, band);
        if (ui::clicked(widget, band) && inputEnabled)
            result.titleClicked = static_cast<int>(i);

        const bool active = static_cast<int>(i) == selected;
        const float hoverT = ui::anim(widget, 0, hot ? 1.f : 0.f, 14.f);
        const float wash = active ? 0.10f : 0.06f * hoverT;
        if (wash > 0.001f)
            ui::dl().rect(band, gold.alpha(wash), 0.f);
        const float bar = active ? 1.f : hoverT * 0.5f;
        if (bar > 0.01f)
            ui::dl().rect(core::Rect(rail.x, band.y, ui::px(3.f), band.h), gold.alpha(bar), 0.f);

        const core::Col ink = gold.alpha(active ? 1.f : 0.55f + 0.35f * hoverT);
        const core::Vec2 titleCenter(centerX, band.y + ui::px(22.f));
        if (entry.icon != nullptr && entry.icon->valid())
            drawMark(entry.icon, titleCenter, ui::px(36.f), ink);
        else
            titleGlyph(titleCenter, ui::px(22.f), ink);
        const core::Rect titleLabel(band.x, band.b() - ui::px(22.f), band.w, ui::px(16.f));
        ui::text(ui::fonts().caption, titleLabel, entry.label, ink, ui::AlignH::Center,
                 ui::AlignV::Middle, ui::px(1.f));

        bandY = band.b() + ui::px(6.f);
    }

    const float cogSize = ui::px(22.f);
    const core::Rect cogBox(centerX - cogSize * 0.5f, cogAxis - cogSize * 0.5f, cogSize, cogSize);
    const std::uint32_t cogId = ui::id("rail.cog");
    const bool cogHot = ui::hovered(cogId, cogBox);
    result.cogClicked = ui::clicked(cogId, cogBox) && inputEnabled;
    const float cogT = ui::anim(cogId, 0, cogHot ? 1.f : 0.f, 16.f);
    if (cogT > 0.01f)
        ui::dl().rect(cogBox.expand(ui::px(7.f)), core::Col::hex(0xFFFFFF, 0.08f * cogT),
                      ui::px(6.f));
    const core::Col cogCol = gold.alpha(0.7f + 0.3f * cogT);
    if (iconsReady())
        drawIcon(IconSize::Rail, icon::cog, cogBox, cogCol);
    else
        cogGlyph(cogBox.center(), cogBox.w * 0.3f, cogCol);

    return result;
}

}
