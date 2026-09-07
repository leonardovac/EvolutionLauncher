#include "ui/ui.h"

namespace ui {

static Context context;

Context& g() { return context; }

// Sizes are multiplied by scale at build time so a DPI change can rebuild the atlas.
static ui::FontData fontData;

bool buildFonts(ID3D11Device* dev, const FontData& data, float scale) {
    Fonts& f = context.fonts;
    bool ok = true;
    auto px = [&](float base) { return base * scale; };

    if (data.bold && data.boldSize) {
        ok &= f.display.createFromMemory(dev, data.bold, data.boldSize, px(30.f), 700.f);
        ok &= f.title.createFromMemory(dev, data.bold, data.boldSize, px(15.f), 700.f);
    } else {
        ok &= f.display.create(dev, L"Segoe UI Variable Display", px(30.f), 700);
        ok &= f.title.create(dev, L"Segoe UI Variable Display", px(15.f), 700);
    }

    if (data.semiBold && data.semiBoldSize) {
        ok &= f.body.createFromMemory(dev, data.semiBold, data.semiBoldSize, px(13.f), 600.f);
        ok &= f.caption.createFromMemory(dev, data.semiBold, data.semiBoldSize, px(11.f), 600.f);
    } else {
        ok &= f.body.create(dev, L"Segoe UI Variable Text", px(13.f), 600);
        ok &= f.caption.create(dev, L"Segoe UI Variable Text", px(11.f), 600);
    }
    return ok;
}

static void destroyFonts() {
    Fonts& f = context.fonts;
    f.display.destroy();
    f.title.destroy();
    f.body.destroy();
    f.caption.destroy();
}

bool init(ID3D11Device* dev, const FontData& data) {
    fontData = data;
    return buildFonts(dev, data, context.scale);
}

bool rebuildFonts(ID3D11Device* dev, float scale) {
    context.scale = scale;
    destroyFonts();
    return buildFonts(dev, fontData, scale);
}

void shutdown() {
    destroyFonts();
    context.anims.clear();
    context.texts.clear();
}

void newFrame(const Input& in, float dt, float width, float height) {
    // A press drops focus before any widget runs; whichever field is hit takes it back
    // the same frame, so clicking away is the only thing that has to be handled.
    if (in.pressed) context.focus = 0;

    context.input = in;
    context.dt = dt;
    context.time += dt;
    context.width = width;
    context.height = height;
    context.hot = context.hotNext;
    context.hotNext = 0;
    context.animated = false;
    context.draw.reset(Rect(0.f, 0.f, width, height));
}

void endFrame() {
    if (!context.input.down) context.active = 0;
}

bool keyPressed(Key key) {
    return (context.input.keys & static_cast<uint32_t>(key)) != 0;
}

void setFocus(uint32_t widget) {
    if (context.focus != widget) context.texts[widget].blink = context.time;
    context.focus = widget;
}

void clearFocus() { context.focus = 0; }

TextState& textState(uint32_t widget) { return context.texts[widget]; }

uint32_t id(std::string_view str) {
    uint32_t h = 2166136261u;
    for (const char ch : str) {
        h ^= static_cast<uint32_t>(static_cast<uint8_t>(ch));
        h *= 16777619u;
    }
    return h ? h : 1u;
}

static AnimState& state(uint32_t widget, int slot) {
    return context.anims[(static_cast<uint64_t>(widget) << 8) | (uint64_t)static_cast<uint8_t>(slot)];
}

float anim(uint32_t widget, int slot, float target, float speed) {
    AnimState& s = state(widget, slot);
    if (!s.initialized) {
        s.initialized = true;
        s.value = target;
        return s.value;
    }
    s.value = core::approach(s.value, target, speed, context.dt);
    if (std::fabs(s.value - target) < 0.0005f) s.value = target;
    else context.animated = true;   // still moving -> keep rendering
    return s.value;
}

float animValue(uint32_t widget, int slot) {
    auto it = context.anims.find((static_cast<uint64_t>(widget) << 8) | (uint64_t)static_cast<uint8_t>(slot));
    return it == context.anims.end() ? 0.f : it->second.value;
}

bool hovered(uint32_t widget, const Rect& r) {
    bool inside = r.contains(context.input.mouse) && r.clipTo(context.draw.currentClip()).contains(context.input.mouse);
    if (inside && (context.active == 0 || context.active == widget)) context.hotNext = widget;
    return inside && context.hot == widget;
}

bool clicked(uint32_t widget, const Rect& r) {
    bool over = hovered(widget, r);
    if (over && context.input.pressed) context.active = widget;
    if (context.active == widget && context.input.released) {
        context.active = 0;
        return over;
    }
    return false;
}

void textShadow(gfx::Font& font, const Rect& box, std::string_view str, const Col& col, AlignH h, AlignV v,
                float tracking, float shadowAlpha, float offsetY) {
    text(font, box.offset(0.f, offsetY), str, Col::hex(0x000000, shadowAlpha * col.a), h, v, tracking);
    text(font, box, str, col, h, v, tracking);
}

void text(gfx::Font& font, const Rect& box, std::string_view str, const Col& col, AlignH h, AlignV v,
          float tracking) {
    if (str.empty()) return;
    float w = font.measure(str);
    if (tracking != 0.f) {
        int count = 0;
        for (size_t i = 0; i < str.size();) { gfx::Font::decode(str, i); ++count; }
        if (count > 0) w += tracking * (count - 1);
    }

    float x = box.x;
    if (h == AlignH::Center) x = box.x + (box.w - w) * 0.5f;
    else if (h == AlignH::Right) x = box.r() - w;

    float y = box.y;
    if (v == AlignV::Middle) y = box.y + (box.h - (font.ascent() + font.descent())) * 0.5f;
    else if (v == AlignV::Bottom) y = box.b() - (font.ascent() + font.descent());

    context.draw.text(&font, Vec2(std::floor(x), std::floor(y)), col, str, tracking);
}

}
