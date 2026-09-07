#include "ui/widgets.h"

namespace ui {

bool closeButton(std::string_view name, const Rect& r) {
    const uint32_t wid = id(name);
    const bool over = hovered(wid, r);
    const bool result = clicked(wid, r);
    const float hoverT = anim(wid, 0, over ? 1.f : 0.f, 16.f);

    if (hoverT > 0.01f)
        dl().circle(r.center(), r.w * 0.5f, Col::hex(0xFFFFFF, 0.14f * hoverT));

    const Vec2 c = r.center();
    const float s = px(4.4f);
    const Col line = Col::hex(0xFFFFFF, 0.82f + 0.18f * hoverT);
    dl().line(Vec2(c.x - s, c.y - s), Vec2(c.x + s, c.y + s), px(1.7f), line);
    dl().line(Vec2(c.x + s, c.y - s), Vec2(c.x - s, c.y + s), px(1.7f), line);
    return result;
}

void chevron(const Vec2& center, float height, const Col& c) {
    const float halfH = height * 0.5f;
    const float halfW = height * 0.552f * 0.5f;
    const float thickness = height * 0.149f;

    dl().chevronShape(Vec2(center.x - halfW, center.y - halfH), Vec2(center.x + halfW, center.y), thickness, c);
}

// Grey level with the better WCAG contrast against `c`, so the caller can animate it.
static float onAccentInk(const Col& c) {
    auto lin = [](float v) { return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); };
    const float L = 0.2126f * lin(c.r) + 0.7152f * lin(c.g) + 0.0722f * lin(c.b);
    const float vsBlack = (L + 0.05f) / 0.05f;
    const float vsWhite = 1.05f / (L + 0.05f);
    return vsBlack >= vsWhite ? 0.05f : 1.f;
}

// Cover-fit UV so the hero fills the card rect without distortion.
static Rect coverUV(gfx::Image* img, const Rect& card, float biasX, float biasY) {
    Rect uv(0.f, 0.f, 1.f, 1.f);
    if (!img || !img->valid() || card.h <= 0.f) return uv;
    const float ia = img->aspect();
    const float ra = card.w / card.h;
    if (ia > ra) uv.w = ra / ia;
    else uv.h = ia / ra;
    uv.x = (1.f - uv.w) * biasX;
    uv.y = (1.f - uv.h) * biasY;
    return uv;
}

void heroCard(const Rect& card, gfx::Image* hero, const Col& accent, float radius, float alpha, float dim) {
    Theme& t = theme();
    if (alpha <= 0.004f) return;

    dl().rect(card, t.body.alpha(alpha), radius);

    if (hero && hero->valid()) {
        const Rect uv = coverUV(hero, card, 0.62f, 0.38f);
        // Neutral dim; an uneven tint colours every hero.
        const float ink = 0.82f * dim;
        dl().imageShaped(hero->srv.get(), card, Col(ink, ink, ink, alpha), card, radius, uv, 0.f);
        return;
    }

    // No art: an accent wash, so an empty card still reads as this product.
    Rect top(card.x, card.y, card.w, card.h * 0.62f);
    dl().gradientVMasked(top, accent.alpha(0.30f * dim * alpha), accent.alpha(0.f), card, radius);
    dl().gradientHMasked(top, Col::hex(0x000000, 0.f), Col::hex(0x000000, 0.35f * alpha), card, radius);
}

void heroOverlay(const Rect& card, float radius, float alpha) {
    Theme& t = theme();
    if (alpha <= 0.004f) return;

    // One flat wash; graded bands showed their seams on light art.
    dl().rect(card, t.scrim.alpha(0.62f * alpha), radius);
    Rect foot(card.x, card.y + card.h * 0.72f, card.w, card.h * 0.28f);
    dl().gradientVMasked(foot, t.scrim.alpha(0.f), t.scrim.alpha(0.30f * alpha), card, radius);
}

void tabStrip(const Rect& r, float radius, float alpha) {
    Theme& t = theme();
    if (alpha <= 0.004f) return;
    // Barely there; each tile carries its own plate.
    dl().rect(r, t.panelFill.alpha(0.38f * alpha), radius);
}

bool iconTab(std::string_view id, const Rect& r, gfx::Image* icon, std::string_view label, bool active,
             const Col& accent, float alpha) {
    Theme& t = theme();
    const uint32_t wid = ui::id(id);
    const bool over = hovered(wid, r);
    const bool result = clicked(wid, r);

    const float hoverT = core::easeOutCubic(anim(wid, 0, over ? 1.f : 0.f, 16.f));
    const float activeT = anim(wid, 1, active ? 1.f : 0.f, 14.f);
    const float radius = px(t.tileRadius);

    // Selection reads as a contrast flip, not a glow.
    dl().rect(r, t.surface.alpha((0.80f + 0.15f * hoverT) * (1.f - activeT) * alpha), radius);
    if (activeT > 0.004f)
        dl().rect(r, accent.alpha(activeT * alpha), radius);

    // Fully opaque when active; a translucent ink lets the accent bleed through.
    const float ink = core::lerp(1.f, onAccentInk(accent), activeT);
    const Col glyph(ink, ink, ink, core::lerp(0.82f + 0.18f * hoverT, 1.f, activeT) * alpha);

    if (icon && icon->valid()) {
        const float s = r.h * 0.58f;
        Rect ir(r.center().x - s * 0.5f, r.center().y - s * 0.5f, s, s);
        dl().image(icon->srv.get(), ir, glyph, 0.f, Rect(0.f, 0.f, 1.f, 1.f), gfx::DrawList::kAlphaMask);
    } else if (!label.empty()) {
        const char mono[2] = { static_cast<char>(std::toupper(static_cast<unsigned char>(label[0]))), 0 };
        ui::text(fonts().title, r, mono, glyph, AlignH::Center, AlignV::Middle, 0.5f);
    }
    return result;
}

// Key on a 40-degree diagonal; `s` is half the glyph length.
static void keyGlyph(const Vec2& c, float s, const Col& col) {
    const float ca = std::cos(-0.70f), sa = std::sin(-0.70f);
    auto P = [&](float x, float y) { return Vec2(c.x + x * ca - y * sa, c.y + x * sa + y * ca); };

    const float bowR = s * 0.40f;
    const float stroke = (std::max)(s * 0.26f, 1.4f);

    dl().arc(P(-s + bowR, 0.f), bowR, (std::max)(s * 0.30f, 1.6f), 0.f, core::kPi * 2.f, col);
    dl().line(P(-s + bowR * 2.f, 0.f), P(s, 0.f), stroke, col);
    dl().line(P(s * 0.30f, 0.f), P(s * 0.30f, s * 0.48f), stroke, col);
    dl().line(P(s * 0.76f, 0.f), P(s * 0.76f, s * 0.34f), stroke, col);
}

static void playGlyph(const Vec2& c, float s, const Col& col) {
    dl().triangle(Vec2(c.x - s * 0.50f, c.y - s * 0.78f), Vec2(c.x - s * 0.50f, c.y + s * 0.78f),
                  Vec2(c.x + s * 0.82f, c.y), col);
}

static void refreshGlyph(const Vec2& c, float s, const Col& col) {
    const float radius = s * 0.72f;
    const float stroke = (std::max)(s * 0.22f, 1.4f);
    dl().arc(c, radius, stroke, 0.85f, core::kPi * 2.f, col);

    const Vec2 tip(c.x + radius, c.y);
    const float wingX = s * 0.28f;
    const float wingY = s * 0.48f;
    dl().line(tip, Vec2(tip.x - wingX, tip.y - wingY), stroke, col);
    dl().line(tip, Vec2(tip.x + wingX, tip.y - wingY), stroke, col);
}

static void blockedGlyph(const Vec2& c, float s, const Col& col) {
    const float radius = s * 0.72f;
    const float stroke = (std::max)(s * 0.22f, 1.4f);
    dl().arc(c, radius, stroke, 0.f, core::kPi * 2.f, col);
    const float inset = radius * 0.62f;
    dl().line(Vec2(c.x - inset, c.y - inset), Vec2(c.x + inset, c.y + inset), stroke, col);
}

bool actionButton(std::string_view id, const Rect& circle, std::string_view label, PillIcon icon, const Col& accent,
                  bool enabled, float alpha, float* outRight) {
    Theme& t = theme();
    const uint32_t wid = ui::id(id);

    const float glyphS = px(8.f);
    const float labelW = fonts().body.measure(label);
    const float expandedW = circle.w + px(6.f) + labelW + px(18.f);

    // Sized from last frame's hover (slot 3) to break the rect/hover cycle.
    const float expandT = core::easeOutCubic(anim(wid, 2, animValue(wid, 3), 14.f));
    Rect r(circle.x, circle.y, core::lerp(circle.w, expandedW, expandT), circle.h);

    const bool over = enabled && hovered(wid, r);
    const bool result = enabled && clicked(wid, r);
    anim(wid, 3, over ? 1.f : 0.f, 1000.f);

    const float hoverT = core::easeOutCubic(anim(wid, 0, over ? 1.f : 0.f, 16.f));
    const float pressT = anim(wid, 1, (over && g().input.down) ? 1.f : 0.f, 26.f);
    const float radius = r.h * 0.5f;
    Rect box = r.offset(0.f, pressT * px(1.f));
    if (outRight) *outRight = r.r();

    if (enabled)
        dl().rect(box, accent.alpha((0.94f + hoverT * 0.06f) * alpha), radius);
    else
        dl().rect(box, accent.alpha(0.34f * alpha), radius);

    const float fgInk = onAccentInk(accent);
    const Col fg = enabled ? Col(fgInk, fgInk, fgInk, alpha) : Col::hex(0xFFFFFF, 0.55f * alpha);

    // The glyph stays pinned so it does not slide under the cursor as the pill grows.
    const Vec2 gc(box.x + circle.w * 0.5f, box.center().y);
    switch (icon) {
    case PillIcon::Key: keyGlyph(gc, glyphS, fg); break;
    case PillIcon::Play: playGlyph(gc, glyphS, fg); break;
    case PillIcon::Refresh: refreshGlyph(gc, glyphS, fg); break;
    case PillIcon::Blocked: blockedGlyph(gc, glyphS, fg); break;
    }

    if (expandT > 0.02f) {
        dl().pushClip(box);
        ui::text(fonts().body, Rect(box.x + circle.w + px(6.f), box.y, labelW, box.h), label,
                 fg.alpha(expandT * expandT), AlignH::Left, AlignV::Middle, 0.3f);
        dl().popClip();
    }
    return result;
}

void infoPanel(const Rect& r, float radius, float alpha) {
    Theme& t = theme();
    if (alpha <= 0.004f) return;

    // Translucent enough to sit on the art rather than cover it.
    dl().rect(r, t.panelFill.alpha(0.58f * alpha), radius);
}

Col animatedColor(std::string_view name, const Col& target, float speed) {
    const uint32_t wid = ui::id(name);
    return Col(anim(wid, 0, target.r, speed), anim(wid, 1, target.g, speed), anim(wid, 2, target.b, speed),
               target.a);
}

void statusDot(std::string_view name, const Vec2& center, float radius, const Col& col, bool alert, float alpha) {
    const uint32_t wid = ui::id(name);
    const float alertT = anim(wid, 0, alert ? 1.f : 0.f, 8.f);

    // Nothing moves; the dot only dims and comes back.
    float breath = 0.f;
    if (alertT > 0.004f) {
        breath = alertT * (0.5f - 0.5f * std::cos(g().time * 2.4f));
        requestFrame();
    }

    dl().shadow(Rect(center.x - radius, center.y - radius, radius * 2.f, radius * 2.f),
                col.alpha(0.45f * alpha), px(8.f), radius, Vec2());
    dl().circle(center, radius, col.alpha(core::lerp(1.f, 0.42f, breath) * alpha));
}

namespace {
struct TextSwap {
    std::string current;
    std::string previous;
    float t = 1.f;
};
std::unordered_map<uint32_t, TextSwap> textSwaps;
constexpr float kTextSwap = 0.22f;
}

void animatedText(std::string_view name, gfx::Font& font, const Rect& box, std::string_view str, const Col& col,
                  AlignH h, float alpha, float tracking) {
    TextSwap& s = textSwaps[ui::id(name)];
    const bool first = s.current.empty() && s.previous.empty() && s.t >= 1.f;
    if (s.current != str) {
        s.previous = s.current;
        s.current = str;
        s.t = first ? 1.f : 0.f;
    }
    if (s.t < 1.f) {
        s.t = core::clamp01(s.t + g().dt / kTextSwap);
        requestFrame();
    }

    // Sequential, not crossed: the old string clears before the new one arrives, so glyphs never smear.
    const float out = core::clamp01(s.t * 2.f);
    const float in = core::clamp01(s.t * 2.f - 1.f);
    if (out < 1.f && !s.previous.empty())
        ui::text(font, box, s.previous, col.alpha((1.f - out) * alpha), h, AlignV::Middle, tracking);
    if (in > 0.f)
        ui::text(font, box, s.current, col.alpha(in * alpha), h, AlignV::Middle, tracking);
}

void infoRow(std::string_view name, const Rect& r, std::string_view label, std::string_view value,
             const Col& valueCol, DotState dot, bool separator, float alpha) {
    Theme& t = theme();

    if (separator)
        dl().rect(Rect(r.x, std::floor(r.y) + 0.5f, r.w, 1.f), Col::hex(0xFFFFFF, 0.06f * alpha), 0.f);

    ui::text(fonts().body, r, label, t.subtext.alpha(0.90f * alpha), AlignH::Left, AlignV::Middle);

    std::string key(name);
    const size_t base = key.size();
    key += ".col";
    const Col ink = dot == DotState::None ? valueCol : animatedColor(key, valueCol);

    if (dot != DotState::None) {
        const float dotR = px(3.f);
        key.resize(base);
        key += ".dot";
        // Glides with the value width so a longer label does not snap the dot sideways.
        const float dotX = anim(ui::id(key), 3, r.r() - fonts().body.measure(value) - px(11.f), 18.f);
        statusDot(key, Vec2(dotX, r.center().y), dotR, ink, dot == DotState::Alert, alpha);
    }

    key.resize(base);
    key += ".value";
    animatedText(key, fonts().body, r, value, ink, AlignH::Right, alpha);
}

void logoMark(gfx::Image* logo, const Rect& box, float alpha) {
    if (!logo || !logo->valid()) return;
    dl().image(logo->srv.get(), box.offset(px(1.5f), px(3.f)), Col(0.f, 0.f, 0.f, 0.34f * alpha));
    dl().image(logo->srv.get(), box, Col(1.f, 1.f, 1.f, alpha));
}

void loadingLine(const Rect& r, float progress, float time, const Col& accent, float alpha) {
    const float radius = r.h * 0.5f;
    const float p = core::clamp01(progress);

    dl().rect(r, Col::hex(0xFFFFFF, 0.07f * alpha), radius);

    if (p <= 0.001f) return;
    Rect fill(r.x, r.y, (std::max)(r.w * p, r.h), r.h);
    dl().shadow(fill.shrink(0.5f), accent.alpha(0.34f * alpha), px(10.f), radius, Vec2());
    dl().gradientHMasked(fill, accent.alpha(0.55f * alpha), accent.alpha(alpha), fill, radius);

    // Travelling highlight, so a sub-second boot still reads as motion; it retires as the bar fills.
    const float head = 1.f - core::clamp01((p - 0.90f) / 0.10f);
    if (head <= 0.004f) return;
    const float pulse = 0.55f + 0.45f * std::sin(time * 5.0f);
    dl().circle(Vec2(fill.r() - r.h * 0.5f, fill.center().y), r.h * 0.42f,
                Col::hex(0xFFFFFF, 0.75f * pulse * head * alpha));
}

}
