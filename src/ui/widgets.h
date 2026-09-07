#pragma once
#include "ui/ui.h"

namespace ui {

bool closeButton(std::string_view name, const Rect& r);
void chevron(const Vec2& center, float height, const Col& c);

// Cover-fit hero art; falls back to an accent gradient when there is none. `dim` scales
// the art itself rather than washing it: a separate scrim would fade at its own rate.
void heroCard(const Rect& card, gfx::Image* hero, const Col& accent, float radius, float alpha,
              float dim = 1.f);

// Uniform wash, so contrast does not vary with vertical position.
void heroOverlay(const Rect& card, float radius, float alpha);

// Container behind the game tiles.
void tabStrip(const Rect& r, float radius, float alpha);

// Icons draw as flat silhouettes (kAlphaMask); `label` is the fallback glyph.
bool iconTab(std::string_view id, const Rect& r, gfx::Image* icon, std::string_view label, bool active,
             const Col& accent, float alpha);

// Vector-drawn: there is no icon font in the atlas.
enum class PillIcon { Key, Play, Refresh, Blocked };

// `outRight` receives the animated right edge so the caller can trail text after it.
bool actionButton(std::string_view id, const Rect& circle, std::string_view label, PillIcon icon, const Col& accent,
                  bool enabled, float alpha, float* outRight);

// Flat translucent container; sampling the art behind it hurt legibility.
void infoPanel(const Rect& r, float radius, float alpha);

// Eases per channel, so a state change slides through the palette instead of cutting.
Col animatedColor(std::string_view id, const Col& target, float speed = 10.f);

// Halo dot; `alert` breathes, which also holds the idle loop open.
void statusDot(std::string_view id, const Vec2& center, float radius, const Col& col, bool alert, float alpha);

// Crossfades on a changed string: the outgoing line rises out as the incoming one settles.
void animatedText(std::string_view id, gfx::Font& font, const Rect& box, std::string_view str, const Col& col,
                  AlignH h, float alpha, float tracking = 0.f);

// Dot that leads an infoRow value.
enum class DotState { None, Steady, Alert };

// Label left, value right; `dot` leads the value with a state dot.
void infoRow(std::string_view id, const Rect& r, std::string_view label, std::string_view value,
             const Col& valueCol, DotState dot, bool separator, float alpha);

void logoMark(gfx::Image* logo, const Rect& box, float alpha);

// Determinate progress track; `progress` in [0,1].
void loadingLine(const Rect& r, float progress, float time, const Col& accent, float alpha);

}
