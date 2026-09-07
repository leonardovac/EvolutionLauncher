#pragma once
#include <string>
#include <string_view>
#include <unordered_map>
#include "core/types.h"
#include "gfx/drawlist.h"
#include "gfx/font.h"
#include "gfx/image.h"

namespace ui {

using core::Col;
using core::Rect;
using core::Vec2;

enum class AlignH { Left, Center, Right };
enum class AlignV { Top, Middle, Bottom };

// Editing keys arrive as a per-frame bitmask; printable input arrives already decoded
// to UTF-8, so nothing below this layer has to know about surrogate pairs.
enum class Key : uint32_t {
    Backspace = 1u << 0,
    Delete    = 1u << 1,
    Left      = 1u << 2,
    Right     = 1u << 3,
    Home      = 1u << 4,
    End       = 1u << 5,
    Enter     = 1u << 6,
    Escape    = 1u << 7,
    SelectAll = 1u << 8,
    Copy      = 1u << 9,
    Cut       = 1u << 10,
    Paste     = 1u << 11,
};

struct Input {
    Vec2 mouse;
    bool down = false;
    bool pressed = false;
    bool released = false;
    bool shift = false;
    std::string typed;      // printable text entered this frame, UTF-8
    uint32_t keys = 0;      // Key bits pressed this frame
};

// Caret and selection survive between frames; the widget itself is stateless.
struct TextState {
    size_t caret = 0;
    size_t anchor = 0;
    float scroll = 0.f;
    float blink = 0.f;
};

struct Theme {
    // Neutral and a hair warm; a blue cast here fights the crimson accent.
    Col body = Col::hex(0x0B0A0A, 1.f);
    Col surface = Col::hex(0x171615, 1.f);   // raised tile / inactive control
    Col text = Col::hex(0xF4F3F2, 1.f);
    Col subtext = Col::hex(0xA3A09E, 1.f);
    Col focus = Col::hex(0xE23B55, 1.f);     // brand accent
    Col focusDeep = Col::hex(0x7A1226, 1.f);
    Col scrim = Col::hex(0x060505, 1.f);     // wash over the hero art
    Col panelFill = Col::hex(0x0F0E0E, 1.f); // details panel / tab strip base
    Col pass = Col::hex(0x3FB950, 1.f);
    Col fail = Col::hex(0xE5484D, 1.f);
    Col warn = Col::hex(0xE2A03B, 1.f);
    Col pending = Col::hex(0x6E6B6A, 1.f);
    // One radius family, biggest to smallest.
    float cardRadius = 12.f;
    float panelRadius = 8.f;
    float tileRadius = 8.f;
    float rowRadius = 6.f;
    float radius = 8.f;
};

struct Fonts {
    gfx::Font display;
    gfx::Font title;
    gfx::Font body;
    gfx::Font caption;
};

struct AnimState {
    float value = 0.f;
    float velocity = 0.f;
    bool initialized = false;
};

struct Context {
    gfx::DrawList draw;
    Input input;
    Theme theme;
    Fonts fonts;
    float dt = 0.f;
    float time = 0.f;
    float width = 0.f;
    float height = 0.f;
    float scale = 1.f;   // DPI scale; multiplies every metric
    bool animated = false;   // set when any anim/spring moved this frame (idle gating)
    uint32_t hot = 0;
    uint32_t active = 0;
    uint32_t hotNext = 0;
    uint32_t focus = 0;
    std::unordered_map<uint64_t, AnimState> anims;
    std::unordered_map<uint32_t, TextState> texts;
};

struct FontData {
    const void* semiBold = nullptr;
    size_t semiBoldSize = 0;
    const void* bold = nullptr;
    size_t boldSize = 0;
};

Context& g();
bool init(ID3D11Device* dev, const FontData& fonts);
bool rebuildFonts(ID3D11Device* dev, float scale);
void shutdown();
inline float scale() { return g().scale; }
inline float px(float v) { return v * g().scale; }   // scale a design-space metric
inline void requestFrame() { g().animated = true; }   // a time-driven effect has to defeat idle gating
void newFrame(const Input& in, float dt, float width, float height);
void endFrame();

inline gfx::DrawList& dl() { return g().draw; }
inline Theme& theme() { return g().theme; }
inline Fonts& fonts() { return g().fonts; }

uint32_t id(std::string_view str);

bool keyPressed(Key key);
// True while a widget is taking keystrokes, so the window proc leaves Escape alone.
inline bool keyboardCaptured() { return g().focus != 0; }
void setFocus(uint32_t widget);
void clearFocus();
TextState& textState(uint32_t widget);

float anim(uint32_t widget, int slot, float target, float speed);
// Reads a slot without driving it, so a widget can size itself from last frame.
float animValue(uint32_t widget, int slot);
bool hovered(uint32_t widget, const Rect& r);
bool clicked(uint32_t widget, const Rect& r);

void text(gfx::Font& font, const Rect& box, std::string_view str, const Col& col, AlignH h = AlignH::Left,
          AlignV v = AlignV::Middle, float tracking = 0.f);
void textShadow(gfx::Font& font, const Rect& box, std::string_view str, const Col& col, AlignH h = AlignH::Left,
                AlignV v = AlignV::Middle, float tracking = 0.f, float shadowAlpha = 0.55f,
                float offsetY = 1.f);

}
