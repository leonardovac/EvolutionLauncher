#include "app/shell.h"

#include "app/controls.h"
#include "app/icons.h"
#include "app/languages.h"
#include "app/rail.h"
#include "app/shelllayout.h"
#include "ui/ui.h"
#include "ui/widgets.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <ranges>
#include <string>

namespace app
{
namespace
{

bool closeClicked = false;
bool startClicked = false;
bool secondaryClicked = false;
bool minimiseClicked = false;
int languageIndex = -1;
bool cogClicked = false;
std::string_view navClicked;

struct NavEntry
{
    std::string_view id;
    std::string_view label;
    std::string_view url;
};

constexpr std::array<NavEntry, 3> navEntries{
    {{"shell.nav.news", "NEWS", "https://www.warframe.com/news"},
     {"shell.nav.notes", "PATCH NOTES", "https://www.warframe.com/patch-notes"},
     {"shell.nav.prime", "PRIME ACCESS", "https://www.warframe.com/prime-access"}}};

void globeGlyph(const core::Vec2& center, float radius, const core::Col& col)
{
    ui::dl().arc(center, radius, ui::px(1.f), 0.f, core::kPi * 2.f, col);
    ui::dl().line(core::Vec2(center.x - radius, center.y), core::Vec2(center.x + radius, center.y),
                  ui::px(1.f), col);
    ui::dl().arc(core::Vec2(center.x, center.y), radius * 0.5f, ui::px(1.f), 0.f, core::kPi * 2.f,
                 col);
}

core::Rect captionGlyphBox(const core::Rect& viewport, int slot)
{
    const float size = ui::px(shellGlyphSize);
    const float top = viewport.y + ui::px(shellHeaderTop) + (ui::px(shellRowHeight) - size) * 0.5f;
    const float right = viewport.r() - ui::px(shellEdgeMargin);
    return core::Rect(right - size - ui::px(shellMinimiseGap) * static_cast<float>(slot), top, size,
                      size);
}

float navWidth(std::string_view label, float tracking)
{
    return ui::fonts().caption.measure(label) +
           tracking * static_cast<float>(label.size() > 0 ? label.size() - 1 : 0);
}

// wide enough for "Chinese (Traditional)", the longest entry, at the caption font
float languageListWidth()
{
    float widest = 0.f;
    for (const std::string_view name : languageNames())
        widest = std::max(widest, ui::fonts().caption.measure(name));
    return widest + ui::px(36.f);
}

void hexFill(const core::Rect& box, const core::Col& col)
{
    const float chamfer = std::min(box.h * 0.45f, box.w * 0.5f);
    const core::Vec2 leftTop(box.x + chamfer, box.y);
    const core::Vec2 rightTop(box.r() - chamfer, box.y);
    const core::Vec2 leftBottom(box.x + chamfer, box.b());
    const core::Vec2 rightBottom(box.r() - chamfer, box.b());
    ui::dl().rect(core::Rect(leftTop.x, box.y, rightTop.x - leftTop.x, box.h), col, 0.f);
    ui::dl().triangle(leftTop, leftBottom, core::Vec2(box.x, box.center().y), col);
    ui::dl().triangle(rightTop, rightBottom, core::Vec2(box.r(), box.center().y), col);
}

void hexFrame(const core::Rect& box, float thickness, const core::Col& col)
{
    const float chamfer = std::min(box.h * 0.45f, box.w * 0.5f);
    const std::array<core::Vec2, 6> points{
        core::Vec2(box.x + chamfer, box.y),
        core::Vec2(box.r() - chamfer, box.y),
        core::Vec2(box.r(), box.center().y),
        core::Vec2(box.r() - chamfer, box.b()),
        core::Vec2(box.x + chamfer, box.b()),
        core::Vec2(box.x, box.center().y)};
    for (std::size_t i = 0; i < points.size(); ++i)
        ui::dl().line(points[i], points[(i + 1) % points.size()], thickness, col);
}

}

void drawShell(const core::Rect& viewport, gfx::Image* hero, const ShellState& state)
{
    using core::Col;
    using core::Rect;
    using core::Vec2;

    const Col gold = Col::hex(0xD9C07A, 1.f);

    ui::dl().rect(viewport, ui::theme().body);
    ui::heroCard(viewport, hero, ui::theme().focus, 0.f, 1.f);
    ui::heroOverlay(viewport, 0.f, 0.35f);

    const Rect content(viewport.x + ui::px(railWidth), viewport.y,
                       viewport.w - ui::px(railWidth), viewport.h);
    const float headerY = content.y + ui::px(shellHeaderTop);
    const float edgeRight = content.r() - ui::px(shellEdgeMargin);

    const float contentLeft = content.x + ui::px(shellContentPad);
    const float ruleY = headerY + ui::px(shellRowHeight) + ui::px(shellHeaderRuleGap);
    ui::dl().line(Vec2(contentLeft, ruleY), Vec2(edgeRight, ruleY), ui::px(1.f), gold.alpha(0.28f));

    navClicked = {};
    const float navTracking = ui::px(2.f);
    float navX = contentLeft;
    for (const NavEntry& entry : navEntries)
    {
        const float entryW = navWidth(entry.label, navTracking) + ui::px(shellNavPad) * 2.f;
        const Rect box(navX, headerY, entryW, ui::px(shellRowHeight));
        const std::uint32_t navId = ui::id(entry.id);
        const bool navHot = ui::hovered(navId, box);
        const bool navHit = ui::clicked(navId, box);
        if (navHit && !state.panelVisible)
            navClicked = entry.url;
        const float navT = ui::anim(navId, 0, navHot ? 1.f : 0.f, 14.f);
        const Col rest = ui::theme().text.alpha(0.72f);
        const Col navCol(core::lerp(rest.r, gold.r, navT), core::lerp(rest.g, gold.g, navT),
                         core::lerp(rest.b, gold.b, navT), core::lerp(rest.a, gold.a, navT));
        ui::text(ui::fonts().caption, box, entry.label, navCol, ui::AlignH::Center,
                 ui::AlignV::Middle, navTracking);
        if (navT > 0.01f)
        {
            const float half = (entryW - ui::px(shellNavPad)) * 0.5f * navT;
            ui::dl().rect(Rect(box.center().x - half, ruleY - ui::px(1.f), half * 2.f, ui::px(2.f)),
                          gold.alpha(navT), ui::px(1.f));
        }
        navX = box.r() + ui::px(shellNavGap);
    }

    const Rect minimiseBox = captionGlyphBox(viewport, 1);
    const float dividerX = minimiseBox.x - ui::px(shellDividerGap);
    // the panel carries its own Language row, so the chip would only ghost through it
    if (!state.panelVisible)
        ui::dl().line(Vec2(dividerX, minimiseBox.center().y - ui::px(7.f)),
                      Vec2(dividerX, minimiseBox.center().y + ui::px(7.f)), ui::px(1.f),
                      gold.alpha(0.3f));

    const float languageW = ui::px(shellLanguageWidth);
    const Rect languageRow(dividerX - ui::px(shellLanguageGap) - languageW, headerY, languageW,
                           ui::px(shellRowHeight));
    // the overlay writes this after every other widget runs, so the compare waits until then
    int languagePick = state.languageIndex;
    Rect globeBox(languageRow.x, languageRow.y, ui::px(13.f), languageRow.h);
    if (!state.panelVisible)
        dropdown(DropdownGroup::Shell, "shell.language", languageRow, "", languageNames(),
                 languagePick, languageShortLabel(languagePick), languageListWidth(), &globeBox);

    // nudged to the caption's optical centre, which sits a pixel above the row's geometric one
    const Rect globeInk = globeBox.offset(0.f, -ui::px(1.f));
    if (!state.panelVisible)
    {
        if (iconsReady())
            drawIcon(IconSize::Caption, icon::globe, globeInk, gold.alpha(0.8f));
        else
            globeGlyph(globeInk.center(), ui::px(7.f), gold.alpha(0.8f));
    }

    // settings sit with the other content controls, not in the rail, which is title selection
    const Rect cogBox(languageRow.x - ui::px(shellCogGap) - ui::px(shellGlyphSize), headerY,
                      ui::px(shellGlyphSize), ui::px(shellRowHeight));
    cogClicked = false;
    if (!state.panelVisible)
    {
        const std::uint32_t cogId = ui::id("shell.cog");
        const bool cogHot = ui::hovered(cogId, cogBox);
        cogClicked = ui::clicked(cogId, cogBox);
        const float cogT = ui::anim(cogId, 0, cogHot ? 1.f : 0.f, 16.f);
        if (cogT > 0.01f)
            ui::dl().rect(cogBox.expand(ui::px(3.f)), Col::hex(0xFFFFFF, 0.09f * cogT),
                          cogBox.h * 0.5f);
        const Col cogCol = gold.alpha(0.7f + 0.3f * cogT);
        if (iconsReady())
            drawIcon(IconSize::Caption, icon::cog, cogBox.offset(0.f, -ui::px(1.f)), cogCol);
        else
            ui::dl().arc(cogBox.center(), ui::px(6.f), ui::px(1.5f), 0.f, core::kPi * 2.f, cogCol);
    }

    // the secondary line hangs below the button, so the whole block lifts to keep its margin
    const float lift = state.secondaryVisible ? ui::px(12.f) : 0.f;
    const Rect bottom(contentLeft, content.b() - ui::px(shellBottomInset + shellBottomHeight) - lift,
                      edgeRight - contentLeft, ui::px(shellBottomHeight));
    ui::dl().line(Vec2(bottom.x, bottom.y - ui::px(14.f)), Vec2(edgeRight, bottom.y - ui::px(14.f)),
                  ui::px(1.f), gold.alpha(0.20f));

    const Rect start(bottom.r() - ui::px(240.f), bottom.y + ui::px(8.f), ui::px(240.f),
                     ui::px(44.f));
    const Col startCol = state.startEnabled ? gold : gold.alpha(0.35f);
    const std::uint32_t startId = ui::id("shell.start");
    const bool startHit = ui::clicked(startId, start);
    startClicked = state.startEnabled && startHit && !state.panelVisible;
    const bool hot = state.startEnabled && ui::hovered(startId, start);
    const float startT = ui::anim(startId, 0, hot ? 1.f : 0.f, 14.f);
    if (startT > 0.01f)
        hexFill(start, gold.alpha(0.14f * startT));
    hexFrame(start, ui::px(1.f), startCol.alpha(0.8f + 0.2f * startT));
    ui::text(ui::fonts().title, start, state.startLabel, startCol, ui::AlignH::Center,
             ui::AlignV::Middle, ui::px(6.f));

    secondaryClicked = false;
    if (state.secondaryVisible)
    {
        constexpr std::string_view secondaryText = "PLAY WITHOUT UPDATING";
        const float secondaryTracking = ui::px(1.f);
        const float textW = navWidth(secondaryText, secondaryTracking);
        // the hit box hugs the text: a button-wide one lights the underline with nothing under it
        const Rect secondary(start.center().x - textW * 0.5f - ui::px(8.f),
                             start.b() + ui::px(6.f), textW + ui::px(16.f), ui::px(16.f));
        const std::uint32_t secondaryId = ui::id("shell.secondary");
        const bool secondaryHot = ui::hovered(secondaryId, secondary);
        secondaryClicked = ui::clicked(secondaryId, secondary) && !state.panelVisible;
        const float secondaryT = ui::anim(secondaryId, 0, secondaryHot ? 1.f : 0.f, 14.f);
        const Col secondaryCol = ui::theme().subtext.alpha(0.7f + 0.3f * secondaryT);
        ui::text(ui::fonts().caption, secondary, secondaryText, secondaryCol, ui::AlignH::Center,
                 ui::AlignV::Middle, secondaryTracking);
        if (secondaryT > 0.01f)
        {
            const float half = textW * 0.5f * secondaryT;
            ui::dl().line(Vec2(secondary.center().x - half, secondary.b() - ui::px(1.f)),
                          Vec2(secondary.center().x + half, secondary.b() - ui::px(1.f)),
                          ui::px(1.f), secondaryCol.alpha(0.6f * secondaryT));
        }
    }

    const float textW = start.x - bottom.x - ui::px(24.f);
    // the track itself sits on the PLAY button's axis; the label and detail flank it
    const float axis = start.center().y;
    constexpr std::array sweepPhases{JobPhase::Idle, JobPhase::Checking};
    constexpr std::array barPhases{JobPhase::Idle, JobPhase::Checking, JobPhase::Updating};
    const bool showBar = std::ranges::contains(barPhases, state.phase);
    if (showBar)
    {
        const Rect track(bottom.x, axis - ui::px(1.5f), textW, ui::px(3.f));
        const Rect label(bottom.x, track.y - ui::px(24.f), textW, ui::px(16.f));
        ui::text(ui::fonts().body, label, state.statusLine, ui::theme().text, ui::AlignH::Left,
                 ui::AlignV::Middle, ui::px(1.2f));
        ui::dl().rect(track, Col::hex(0x000000, 0.45f), track.h * 0.5f);
        ui::dl().rect(track, gold.alpha(0.16f), track.h * 0.5f);
        if (std::ranges::contains(sweepPhases, state.phase))
        {
            const float sweep = 0.5f + 0.5f * std::sin(ui::g().time * 2.2f);
            const float head = track.w * 0.22f;
            const Rect segment(track.x + (track.w - head) * sweep, track.y, head, track.h);
            ui::dl().rect(segment, gold.alpha(0.85f), track.h * 0.5f);
            ui::requestFrame();
        }
        else
        {
            ui::loadingLine(track, state.progress, ui::g().time, gold, 1.f);
        }
        if (!state.detailLine.empty())
        {
            const Rect detail(bottom.x, track.b() + ui::px(8.f), textW, ui::px(14.f));
            ui::text(ui::fonts().caption, detail, state.detailLine, ui::theme().subtext,
                     ui::AlignH::Left, ui::AlignV::Middle, ui::px(1.f));
        }
    }
    else
    {
        const Col line = state.phase == JobPhase::Failed ? ui::theme().fail : ui::theme().subtext;
        const Rect label(bottom.x, axis - ui::px(8.f), textW, ui::px(16.f));
        constexpr std::array labelPhases{JobPhase::Ready, JobPhase::UpdateReady};
        const std::string_view text = std::ranges::contains(labelPhases, state.phase)
            ? state.buildLabel
            : state.statusLine;
        ui::text(ui::fonts().body, label, text, line, ui::AlignH::Left, ui::AlignV::Middle,
                 ui::px(1.2f));
    }

    if (!state.panelVisible)
        dropdownOverlay(DropdownGroup::Shell);
    languageIndex = languagePick != state.languageIndex ? languagePick : -1;
}

void drawWindowControls(const core::Rect& viewport)
{
    using core::Col;
    using core::Rect;
    using core::Vec2;

    const Col gold = Col::hex(0xD9C07A, 1.f);

    const Rect closeBox = captionGlyphBox(viewport, 0);
    const std::uint32_t closeId = ui::id("shell.close");
    const bool closeHot = ui::hovered(closeId, closeBox);
    closeClicked = ui::clicked(closeId, closeBox);
    const float closeT = ui::anim(closeId, 0, closeHot ? 1.f : 0.f, 16.f);
    if (closeT > 0.01f)
        ui::dl().rect(closeBox.expand(ui::px(4.f)), Col::hex(0xFFFFFF, 0.10f * closeT),
                      closeBox.w * 0.5f);
    if (iconsReady())
        drawIcon(IconSize::Caption, icon::close, closeBox, gold.alpha(0.7f + 0.3f * closeT));
    else
        ui::closeButton("shell.close.fallback", closeBox);

    const Rect minimiseBox = captionGlyphBox(viewport, 1);
    const std::uint32_t minimiseId = ui::id("shell.minimise");
    const bool minimiseHot = ui::hovered(minimiseId, minimiseBox);
    minimiseClicked = ui::clicked(minimiseId, minimiseBox);
    const float minimiseT = ui::anim(minimiseId, 0, minimiseHot ? 1.f : 0.f, 16.f);
    if (minimiseT > 0.01f)
        ui::dl().rect(minimiseBox.expand(ui::px(4.f)), Col::hex(0xFFFFFF, 0.10f * minimiseT),
                      minimiseBox.w * 0.5f);
    const Col minimiseCol = gold.alpha(0.7f + 0.3f * minimiseT);
    if (iconsReady())
    {
        drawIcon(IconSize::Caption, icon::minimise, minimiseBox, minimiseCol);
    }
    else
    {
        const Vec2 center = minimiseBox.center();
        ui::dl().line(Vec2(center.x - ui::px(6.f), center.y),
                      Vec2(center.x + ui::px(6.f), center.y), ui::px(1.5f), minimiseCol);
    }
}

bool shellCloseClicked()
{
    return closeClicked;
}

bool shellStartClicked()
{
    return startClicked;
}

bool shellSecondaryClicked()
{
    return secondaryClicked;
}

bool shellMinimiseClicked()
{
    return minimiseClicked;
}

int shellLanguageIndex()
{
    return languageIndex;
}

bool shellCogClicked()
{
    return cogClicked;
}

std::string_view shellNavClicked()
{
    return navClicked;
}

}
