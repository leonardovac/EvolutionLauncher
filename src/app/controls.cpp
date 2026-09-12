#include "app/controls.h"

#include "ui/ui.h"
#include "ui/widgets.h"

namespace app
{
namespace
{

const core::Col gold = core::Col::hex(0xD9C07A, 1.f);

// set by dropdown(), consumed by dropdownOverlay() in the same frame
std::uint32_t openWidget = 0;
DropdownGroup openGroup = DropdownGroup::Shell;
core::Rect openRow;
std::span<const std::string_view> openOptions;
int* openIndex = nullptr;

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
              std::string_view label, std::span<const std::string_view> options, int& index)
{
    const std::uint32_t widget = ui::id(id);
    const core::Rect field(row.x + row.w * 0.45f, row.y, row.w * 0.55f, row.h);
    const bool hit = ui::clicked(widget, field);
    const bool hot = ui::hovered(widget, field);
    if (hit)
    {
        openWidget = openWidget == widget ? 0 : widget;
        openGroup = group;
    }

    const core::Rect caption(row.x, row.y, row.w * 0.45f, row.h);
    ui::text(ui::fonts().caption, caption, label, ui::theme().text, ui::AlignH::Left,
             ui::AlignV::Middle, ui::px(1.f));

    ui::dl().rect(field, ui::theme().surface, ui::px(2.f));
    ui::dl().border(field, hot || openWidget == widget ? gold : gold.alpha(0.45f), ui::px(1.f),
                    ui::px(2.f));

    const bool valid = index >= 0 && index < static_cast<int>(options.size());
    const core::Rect value(field.x + ui::px(8.f), field.y, field.w - ui::px(28.f), field.h);
    ui::text(ui::fonts().caption, value, valid ? options[static_cast<std::size_t>(index)] : "",
             ui::theme().text, ui::AlignH::Left, ui::AlignV::Middle, ui::px(1.f));
    ui::chevron(core::Vec2(field.x + field.w - ui::px(14.f), field.y + field.h * 0.5f),
                ui::px(5.f), gold);

    if (openWidget == widget)
    {
        openRow = field;
        openOptions = options;
        openIndex = &index;
    }
}

bool dropdownOverlay(DropdownGroup group)
{
    if (openWidget == 0 || openIndex == nullptr || openGroup != group)
        return false;

    // a list is time-independent but must not let the idle gate park the frame loop
    ui::requestFrame();

    const float line = ui::px(22.f);
    const float height = line * static_cast<float>(openOptions.size());
    const core::Rect list(openRow.x, openRow.y + openRow.h, openRow.w, height);
    ui::dl().rect(list, ui::theme().panelFill.alpha(0.98f), ui::px(2.f));
    ui::dl().border(list, gold.alpha(0.6f), ui::px(1.f), ui::px(2.f));

    bool changed = false;
    for (std::size_t i = 0; i < openOptions.size(); ++i)
    {
        const core::Rect item(list.x, list.y + line * static_cast<float>(i), list.w, line);
        const std::uint32_t widget = openWidget + static_cast<std::uint32_t>(i) + 1u;
        const bool hot = ui::hovered(widget, item);
        if (hot)
            ui::dl().rect(item, gold.alpha(0.12f), 0.f);
        const bool selected = static_cast<int>(i) == *openIndex;
        const core::Rect caption(item.x + ui::px(8.f), item.y, item.w - ui::px(8.f), item.h);
        ui::text(ui::fonts().caption, caption, openOptions[i],
                 selected ? gold : ui::theme().subtext, ui::AlignH::Left, ui::AlignV::Middle,
                 ui::px(1.f));
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
