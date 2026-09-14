#include "app/icons.h"

#include "gfx/com.h"
#include "gfx/font.h"
#include "ui/ui.h"

#include <dwrite.h>

#include <array>
#include <cstdint>

#pragma comment(lib, "dwrite.lib")

namespace app
{
namespace
{

constexpr std::array<const wchar_t*, 2> families{L"Segoe Fluent Icons", L"Segoe MDL2 Assets"};

gfx::Font captionFont;
gfx::Font railFont;
bool ready = false;

// Font::create silently falls back to Segoe UI, so the family has to be probed first
const wchar_t* installedFamily()
{
    gfx::ComPtr<IDWriteFactory> factory;
    if (FAILED(::DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                     reinterpret_cast<IUnknown**>(factory.put()))))
        return nullptr;

    gfx::ComPtr<IDWriteFontCollection> collection;
    if (FAILED(factory->GetSystemFontCollection(collection.put(), FALSE)))
        return nullptr;

    for (const wchar_t* name : families)
    {
        std::uint32_t index = 0;
        BOOL exists = FALSE;
        if (SUCCEEDED(collection->FindFamilyName(name, &index, &exists)) && exists != FALSE)
            return name;
    }
    return nullptr;
}

}

bool buildIcons(ID3D11Device* dev, float scale)
{
    destroyIcons();
    const wchar_t* family = installedFamily();
    if (family == nullptr)
        return false;
    ready = captionFont.create(dev, family, 14.f * scale, 400) &&
            railFont.create(dev, family, 19.f * scale, 400);
    return ready;
}

void destroyIcons()
{
    captionFont.destroy();
    railFont.destroy();
    ready = false;
}

bool iconsReady()
{
    return ready;
}

void drawIcon(IconSize size, std::string_view glyph, const core::Rect& box, const core::Col& col)
{
    if (!ready)
        return;
    gfx::Font& font = size == IconSize::Rail ? railFont : captionFont;
    ui::text(font, box, glyph, col, ui::AlignH::Center, ui::AlignV::Middle);
}

}
