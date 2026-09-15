#include "app/controls.h"

#include "ui/ui.h"

#include <algorithm>

namespace app
{
namespace
{

const core::Col gold = core::Col::hex(0xD9C07A, 1.f);

// ui::text applies tracking between glyphs but Font::measure does not
float trackedWidth(std::string_view str, float tracking)
{
    float width = ui::fonts().caption.measure(str);
    int count = 0;
    for (std::size_t i = 0; i < str.size();)
    {
        gfx::Font::decode(str, i);
        ++count;
    }
    return count > 1 ? width + tracking * static_cast<float>(count - 1) : width;
}

// ui::chevron points right; a field needs one that points at the list it opens
void fieldChevron(const core::Vec2& center, float half, float openT, const core::Col& col)
{
    const float dy = core::lerp(half * 0.55f, -half * 0.55f, openT);
    const core::Vec2 tip(center.x, center.y + dy);
    ui::dl().line(core::Vec2(center.x - half, center.y - dy), tip, ui::px(1.4f), col);
    ui::dl().line(tip, core::Vec2(center.x + half, center.y - dy), ui::px(1.4f), col);
}

// set by dropdown(), consumed by dropdownOverlay() in the same frame
std::uint32_t openWidget = 0;
DropdownGroup openGroup = DropdownGroup::Shell;
core::Rect openRow;
std::span<const std::string_view> openOptions;
int* openIndex = nullptr;
float openListWidth = 0.f;
float openSlide = 0.f;

}

bool checkbox(std::string_view id, const core::Rect& row, std::string_view label, bool& value)
{
    const std::uint32_t widget = ui::id(id);
    const bool hit = ui::clicked(widget, row);
    const bool hot = ui::hovered(widget, row);
    if (hit)
        value = !value;

    const float side = ui::px(16.f);
    const core::Rect box(row.x, row.y + (row.h - side) * 0.5f, side, side);
    ui::dl().rect(box, ui::theme().surface, ui::px(2.f));
    ui::dl().border(box, hot ? gold : gold.alpha(0.6f), ui::px(1.f), ui::px(2.f));
    if (value)
    {
        const core::Rect mark(box.x + ui::px(4.f), box.y + ui::px(4.f), side - ui::px(8.f),
                              side - ui::px(8.f));
        ui::dl().rect(mark, gold, ui::px(1.f));
    }

    const core::Rect caption(row.x + ui::px(26.f), row.y, row.w - ui::px(26.f), row.h);
    ui::text(ui::fonts().caption, caption, label, ui::theme().text, ui::AlignH::Left,
             ui::AlignV::Middle, ui::px(1.f));
    return hit;
}

void dropdown(DropdownGroup group, std::string_view id, const core::Rect& row,
              std::string_view label, std::span<const std::string_view> options, int& index,
              std::string_view display, float listWidth, core::Rect* outLead)
{
    const std::uint32_t widget = ui::id(id);
    // a shell chip is chrome-free and fills its row; a panel row splits into label and field
    const bool bare = group == DropdownGroup::Shell;
    const core::Rect field =
        bare ? row : core::Rect(row.x + row.w * 0.45f, row.y, row.w * 0.55f, row.h);
    const bool hit = ui::clicked(widget, field);
    const bool hot = ui::hovered(widget, field);
    if (hit)
    {
        openWidget = openWidget == widget ? 0 : widget;
        openGroup = group;
    }
    const bool open = openWidget == widget;

    // driven every frame so dropdownOverlay can ease the list in from the closed state
    const float openT = ui::anim(widget, 1, open ? 1.f : 0.f, 18.f);
    const float hoverT = ui::anim(widget, 0, hot || open ? 1.f : 0.f, 16.f);

    if (bare)
    {
        if (hoverT > 0.01f)
            ui::dl().rect(field, core::Col::hex(0xFFFFFF, 0.09f * hoverT), ui::px(2.f));
    }
    else
    {
        const core::Rect caption(row.x, row.y, row.w * 0.45f, row.h);
        ui::text(ui::fonts().caption, caption, label, ui::theme().text, ui::AlignH::Left,
                 ui::AlignV::Middle, ui::px(1.f));
        ui::dl().rect(field, ui::theme().surface, ui::px(2.f));
        ui::dl().border(field, gold.alpha(0.45f + 0.55f * hoverT), ui::px(1.f), ui::px(2.f));
    }

    const bool valid = index >= 0 && index < static_cast<int>(options.size());
    const std::string_view valueText =
        !display.empty() ? display : (valid ? options[static_cast<std::size_t>(index)] : "");
    const float tracking = ui::px(1.f);

    core::Rect value;
    float chevronX = 0.f;
    if (bare)
    {
        // glyph, value and chevron centre as one group, so the chip has symmetric slack;
        // the slot hugs the glyph ink or the gap after it reads wider than the one before the chevron
        const float lead = ui::px(13.f);
        const float gap = ui::px(7.f);
        const float chevW = ui::px(9.f);
        const float textW = trackedWidth(valueText, tracking);
        const float groupX = field.x + (field.w - (lead + gap + textW + gap + chevW)) * 0.5f;
        if (outLead != nullptr)
            *outLead = core::Rect(groupX, field.y, lead, field.h);
        value = core::Rect(groupX + lead + gap, field.y, textW, field.h);
        chevronX = value.r() + gap + chevW * 0.5f;
    }
    else
    {
        value = core::Rect(field.x + ui::px(8.f), field.y, field.w - ui::px(28.f), field.h);
        chevronX = field.r() - ui::px(10.f);
    }

    ui::text(ui::fonts().caption, value, valueText, ui::theme().text.alpha(0.85f + 0.15f * hoverT),
             ui::AlignH::Left, ui::AlignV::Middle, tracking);
    fieldChevron(core::Vec2(chevronX, field.center().y), ui::px(4.f), openT,
                 gold.alpha(0.7f + 0.3f * hoverT));

    if (open)
    {
        openRow = field;
        openOptions = options;
        openIndex = &index;
        openListWidth = listWidth > 0.f ? listWidth : field.w;
        openSlide = openT;
    }
}

bool dropdownOverlay(DropdownGroup group)
{
    if (openWidget == 0 || openIndex == nullptr || openGroup != group)
        return false;

    // a list is time-independent but must not let the idle gate park the frame loop
    ui::requestFrame();

    const float line = ui::px(26.f);
    const float pad = ui::px(5.f);
    const float radius = ui::px(3.f);
    const float height = line * static_cast<float>(openOptions.size()) + pad * 2.f;
    const float listX = std::max(0.f, openRow.r() - openListWidth);
    const float slide = openSlide;
    const core::Rect list(listX, openRow.b() + ui::px(6.f) - ui::px(8.f) * (1.f - slide),
                          openListWidth, height);

    // a press that lands on neither the field nor the list dismisses it
    if (ui::g().input.pressed && !list.contains(ui::g().input.mouse) &&
        !openRow.contains(ui::g().input.mouse))
    {
        closeDropdown();
        return false;
    }

    ui::dl().shadow(list, core::Col::hex(0x000000, 0.6f * slide), ui::px(20.f), radius,
                    core::Vec2(0.f, ui::px(5.f)));
    ui::dl().rect(list, core::Col::hex(0x0B0A0A, 0.97f * slide), radius);
    ui::dl().border(list, gold.alpha(0.32f * slide), ui::px(1.f), radius);

    bool changed = false;
    for (std::size_t i = 0; i < openOptions.size(); ++i)
    {
        const core::Rect item(list.x, list.y + pad + line * static_cast<float>(i), list.w, line);
        const std::uint32_t widget = openWidget + static_cast<std::uint32_t>(i) + 1u;
        const bool hot = ui::hovered(widget, item);
        const bool selected = static_cast<int>(i) == *openIndex;
        const float rowT = ui::anim(widget, 0, hot ? 1.f : 0.f, 20.f);
        if (rowT > 0.01f)
            ui::dl().rect(item, gold.alpha(0.14f * rowT * slide), 0.f);
        const float markT = std::max(rowT * 0.55f, selected ? 1.f : 0.f);
        if (markT > 0.01f)
            ui::dl().rect(core::Rect(item.x, item.y, ui::px(2.f), item.h),
                          gold.alpha(markT * slide), 0.f);
        const core::Rect caption(item.x + ui::px(12.f) + ui::px(3.f) * rowT, item.y,
                                 item.w - ui::px(20.f), item.h);
        ui::text(ui::fonts().caption, caption, openOptions[i],
                 (selected ? gold : ui::theme().text).alpha((selected ? 1.f : 0.7f + 0.25f * rowT) *
                                                            slide),
                 ui::AlignH::Left, ui::AlignV::Middle, ui::px(1.f));
        if (ui::clicked(widget, item))
        {
            *openIndex = static_cast<int>(i);
            changed = true;
        }
    }

    if (changed)
    {
        openWidget = 0;
        openIndex = nullptr;
    }
    return changed;
}

void closeDropdown()
{
    openWidget = 0;
    openIndex = nullptr;
}

}
