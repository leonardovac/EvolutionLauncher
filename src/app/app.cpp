#include "app/app.h"

#include "app/assets.h"
#include "app/controls.h"
#include "app/languages.h"
#include "app/launch.h"
#include "app/rail.h"
#include "app/railmenu.h"
#include "app/rate.h"
#include "app/resource.h"
#include "app/settings.h"
#include "app/settingspanel.h"
#include "app/shell.h"
#include "app/updatejob.h"
#include "app/versions.h"
#include "app/window.h"
#include "core/log.h"
#include "core/str.h"
#include "core/types.h"
#include "gfx/device.h"
#include "gfx/image.h"
#include "gfx/renderer.h"
#include "ui/ui.h"
#include "update/plan.h"

#include <objbase.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <format>
#include <ranges>

namespace app
{
namespace
{

constexpr int designWidth = 1180;
constexpr int designHeight = 740;

}

int run(const Options& options)
{
    ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    Window window;
    if (!window.create(designWidth, designHeight))
        return 1;

    gfx::Device device;
    if (!device.create(window.handle(), window.width(), window.height()))
        return 1;

    gfx::Renderer renderer;
    if (!renderer.create(device.dev()))
        return 1;

    const auto fontBytes = resource(RES_FONT);
    ui::FontData fontData;
    fontData.semiBold = fontBytes.data();
    fontData.semiBoldSize = fontBytes.size();
    fontData.bold = fontBytes.data();
    fontData.boldSize = fontBytes.size();
    if (!ui::init(device.dev(), fontData))
        return 1;
    ui::rebuildFonts(device.dev(), window.scale());

    gfx::Image hero;
    if (const auto bytes = resource(RES_HERO); !bytes.empty())
    {
        if (auto loaded = gfx::loadImageMemory(device.dev(), bytes))
            hero = std::move(*loaded);
    }

    UpdateJob job;
    if (!options.wantShot)
        job.start();
    RateMeter meter;

    Settings settings = Settings::load();
    Settings working;
    bool panelOpen = options.wantPanel;
    bool saveFailed = false;
    std::string launchFailure;
    bool defragStarted = false;
    float panelSlide = options.wantPanel ? 1.f : 0.f;
    if (options.wantPanel)
        working = settings;

    MenuState menu;
    bool menuOpen = options.wantMenu;
    float menuSlide = options.wantMenu ? 1.f : 0.f;

    auto previous = std::chrono::steady_clock::now();
    float elapsed = 0.f;
    int result = 0;

    while (window.pump())
    {
        const auto now = std::chrono::steady_clock::now();
        const float dt = std::chrono::duration<float>(now - previous).count();
        previous = now;
        elapsed += dt;

        panelSlide = core::clamp01(panelSlide + (panelOpen ? 1.f : -1.f) * dt * 6.f);
        menuSlide = core::clamp01(menuSlide + (menuOpen ? 1.f : -1.f) * dt * 6.f);

        const JobSnapshot snap = job.snapshot();
        if (snap.phase == JobPhase::Updating)
            meter.sample(snap.downloaded, dt);

        if (window.takeResized())
            device.resize(window.width(), window.height());
        if (window.takeScaleChanged())
            ui::rebuildFonts(device.dev(), window.scale());

        device.waitForFrame();
        device.beginFrame();

        const core::Rect viewport(0.f, 0.f, static_cast<float>(device.width()),
                                  static_cast<float>(device.height()));

        ui::Input input;
        input.mouse = window.mousePos();
        input.down = window.mouseDown();
        input.pressed = window.mousePressed();
        input.released = window.mouseReleased();
        ui::newFrame(input, dt, static_cast<float>(device.width()),
                     static_cast<float>(device.height()));
        constexpr std::array liveJobPhases{JobPhase::Checking, JobPhase::Updating};
        if (std::ranges::contains(liveJobPhases, snap.phase))
            ui::requestFrame();
        ShellState shell;
        shell.phase = snap.phase;
        shell.startEnabled = snap.phase == JobPhase::Ready;
        shell.panelVisible = panelSlide > 0.f || menuSlide > 0.f;
        std::string statusBuffer;
        std::string detailBuffer;
        shell.languageIndex = languageIndexFromCode(settings.language);
        switch (snap.phase)
        {
        case JobPhase::Idle:
        case JobPhase::Checking:
            shell.statusLine = "CHECKING FOR UPDATES";
            break;
        case JobPhase::Updating:
        {
            const float fraction =
                snap.downloadTotal != 0
                    ? core::clamp01(static_cast<float>(static_cast<double>(snap.downloaded)
                                                        / static_cast<double>(snap.downloadTotal)))
                    : 0.f;
            shell.progress = fraction;
            statusBuffer = std::format("UPDATING GAME  {}%", static_cast<int>(fraction * 100.f));
            shell.statusLine = statusBuffer;
            detailBuffer = std::format("{} / {}", core::formatBytes(snap.downloaded),
                                       core::formatBytes(snap.downloadTotal));
            if (meter.ready())
            {
                if (const std::string rate = formatRate(meter.bytesPerSecond()); !rate.empty())
                    detailBuffer += std::format("  •  {}", rate);
                const std::uint64_t remaining = snap.downloadTotal > snap.downloaded
                    ? snap.downloadTotal - snap.downloaded
                    : 0u;
                if (const std::string eta = formatEta(remaining, meter.bytesPerSecond());
                    !eta.empty())
                    detailBuffer += std::format("  •  {}", eta);
            }
            shell.detailLine = detailBuffer;
            break;
        }
        case JobPhase::Ready:
            shell.buildLabel = launchFailure.empty() ? "READY" : std::string_view(launchFailure);
            break;
        case JobPhase::Failed:
            shell.statusLine = snap.message ? std::string_view(*snap.message) : "UPDATE FAILED";
            break;
        case JobPhase::Cancelled:
            shell.statusLine = "CANCELLED";
            break;
        }
        drawShell(viewport, hero.valid() ? &hero : nullptr, shell);
        const RailResult rail = drawRail(viewport, !shell.panelVisible);
        if (shellCloseClicked())
        {
            job.cancel();
            break;
        }
        if (shellMinimiseClicked())
            window.minimise();
        if (const std::string_view url = shellNavClicked(); !url.empty())
            ::ShellExecuteW(nullptr, L"open", core::widen(url).c_str(), nullptr, nullptr,
                            SW_SHOWNORMAL);
        if (const int picked = shellLanguageIndex(); picked >= 0)
        {
            Settings next = settings;
            next.language = std::wstring(languageCodeFromIndex(picked));
            if (next.language != settings.language)
            {
                if (next.save(&settings))
                {
                    settings = next;
                    if (!options.wantShot)
                    {
                        meter.reset();
                        job.restart();
                    }
                }
                else
                {
                    core::error("could not write the launcher settings");
                }
            }
            ui::requestFrame();
        }
        if (shellStartClicked())
        {
            if (const auto launched = launchGame(settings, wf::Branch::Public); launched)
            {
                job.cancel();
                break;
            }
            else
            {
                launchFailure = core::narrow(describe(launched.error()));
                core::error("{}", launchFailure);
            }
        }
        if (rail.cogClicked)
        {
            menuOpen = !menuOpen;
            if (menuOpen)
                menu.view = MenuView::Rows;
            closeDropdown();
            ui::requestFrame();
        }
        if (panelSlide > 0.f)
        {
            const PanelResult panelResult =
                drawSettingsPanel(viewport, panelSlide, working, saveFailed);
            constexpr std::array closingResults{PanelResult::Cancelled, PanelResult::Accepted};
            if (std::ranges::contains(closingResults, panelResult))
            {
                if (panelResult == PanelResult::Accepted)
                {
                    const bool needsRecheck = working.language != settings.language
                        || working.graphicsApi != settings.graphicsApi;
                    if (working.save(&settings))
                    {
                        settings = working;
                        saveFailed = false;
                        panelOpen = false;
                        closeDropdown();
                        if (needsRecheck && !options.wantShot)
                        {
                            meter.reset();
                            job.restart();
                        }
                    }
                    else
                    {
                        core::error("could not write the launcher settings");
                        saveFailed = true;
                    }
                }
                else
                {
                    panelOpen = false;
                    closeDropdown();
                }
                ui::requestFrame();
            }
        }
        // outside the menu's slide test: the walk must be reaped even if the panel is gone
        if (menu.optimizeRunning && !job.running())
        {
            menu.optimizeRunning = false;
            menu.optimizeLine = std::format("{} UNLISTED FILES, {}", job.staleFiles(),
                                            core::formatBytes(job.staleBytes()));
            // a stale walk leaves the job idle, not ready; a real check finds out which
            if (!options.wantShot)
            {
                meter.reset();
                job.restart();
            }
            ui::requestFrame();
        }
        if (menuSlide > 0.f)
        {
            switch (drawRailMenu(viewport, menuSlide, menu))
            {
            case MenuAction::Settings:
                menuOpen = false;
                panelOpen = true;
                working = settings;
                saveFailed = false;
                closeDropdown();
                ui::requestFrame();
                break;
            case MenuAction::Verify:
                menuOpen = false;
                if (!options.wantShot)
                {
                    meter.reset();
                    job.restart(true);
                }
                ui::requestFrame();
                break;
            case MenuAction::Versions:
            {
                menu.view = MenuView::Versions;
                menu.launcherLine =
                    std::format("LAUNCHER   {}", core::narrow(launcherVersion()));
                const auto engine = engineVersion(settings, wf::Branch::Public);
                menu.engineLine = engine ? std::format("ENGINE   {}", core::narrow(*engine))
                                         : std::string();
                ui::requestFrame();
                break;
            }
            case MenuAction::Optimize:
                menu.view = MenuView::Optimize;
                menu.optimizeLine.clear();
                menu.defragLine.clear();
                if (!options.wantShot)
                {
                    menu.optimizeRunning = true;
                    job.startStaleReport();
                }
                ui::requestFrame();
                break;
            case MenuAction::Back:
                menu.view = MenuView::Rows;
                ui::requestFrame();
                break;
            case MenuAction::Dismiss:
                menuOpen = false;
                ui::requestFrame();
                break;
            case MenuAction::Defragment:
                if (!options.wantShot)
                {
                    if (const auto started = launchDefrag(settings, wf::Branch::Public); started)
                    {
                        job.cancel();
                        menuOpen = false;
                        defragStarted = true;
                    }
                    else
                    {
                        menu.defragLine = core::narrow(describe(started.error()));
                        core::error("{}", menu.defragLine);
                    }
                }
                ui::requestFrame();
                break;
            case MenuAction::None:
                break;
            }
        }
        ui::endFrame();
        window.clearMouseEdge();
        renderer.render(device.ctx(), ui::dl(), device.width(), device.height());

        if (options.wantShot && elapsed >= options.shotTime)
        {
            if (!device.captureBackbuffer(options.shotPath))
                result = 1;
            break;
        }

        if (defragStarted)
            break;

        device.present(true);
        if (!ui::g().animated && !window.mouseDown())
            window.waitForInput();
    }

    job.cancel();
    job.join();

    hero = gfx::Image{};
    ui::shutdown();
    renderer.destroy();
    device.destroy();
    window.destroy();
    gfx::shutdownImaging();
    ::CoUninitialize();
    return result;
}

}
