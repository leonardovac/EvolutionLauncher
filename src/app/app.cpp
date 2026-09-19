#include "app/app.h"

#include "app/assets.h"
#include "app/controls.h"
#include "app/defragjob.h"
#include "app/heroart.h"
#include "app/icons.h"
#include "app/languages.h"
#include "app/launch.h"
#include "app/locate.h"
#include "app/rail.h"
#include "app/panel.h"
#include "app/rate.h"
#include "app/resource.h"
#include "app/settings.h"
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
#include <string_view>

namespace app
{
namespace
{

// 16:9, so the key art fills the window edge to edge with nothing cropped
constexpr int designWidth = 1180;
constexpr int designHeight = 664;

// a verify and a stale walk both run through JobPhase::Checking, so the label has to say which
std::string_view checkLabel(const JobSnapshot& snap)
{
    if (snap.scanning)
        return "SCANNING FOR UNLISTED FILES";
    if (snap.verifying)
        return "VERIFYING THE INSTALL";
    return "CHECKING FOR UPDATES";
}

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
    buildIcons(device.dev(), window.scale());

    gfx::Image warframeHero;
    gfx::Image publisherLogo;
    gfx::Image warframeIcon;
    const auto loadArt = [&device](int id, gfx::Image& out) {
        if (const auto bytes = resource(id); !bytes.empty())
        {
            if (auto loaded = gfx::loadImageMemory(device.dev(), bytes))
                out = std::move(*loaded);
        }
    };
    loadArt(RES_WARFRAME_HERO, warframeHero);
    loadArt(RES_PUBLISHER_LOGO, publisherLogo);
    loadArt(RES_WARFRAME_ICON, warframeIcon);

    const std::array<RailTitle, 1> railTitles{
        {{"rail.warframe", "WARFRAME", &warframeIcon, &warframeHero}}};
    int selectedTitle = 0;

    UpdateJob job;
    if (!options.wantShot)
        job.start();
    RateMeter meter;

    Settings settings = Settings::load(wf::Title::Warframe, wf::LauncherConfig::load());
    Settings working;
    PanelState panel;
    const bool startOpen = options.wantPanel || options.wantMenu;
    bool panelOpen = startOpen;
    std::string launchFailure;
    float panelSlide = startOpen ? 1.f : 0.f;
    if (options.wantMenu)
        panel.tab = PanelTab::Maintenance;
    if (startOpen)
        working = settings;

    DefragJob defrag;
    float defragNotice = 0.f;
    std::uint32_t defragExit = 0;

    HeroArt heroArt;
    gfx::Image liveHero;
    float heroFade = 0.f;
    // a screenshot has to be reproducible, so it keeps the baked-in art
    if (!options.wantShot)
        heroArt.start();


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

        if (!liveHero.valid())
        {
            if (const auto art = heroArt.take(); !art.empty())
            {
                if (auto loaded = gfx::loadImageMemory(device.dev(), art))
                {
                    liveHero = std::move(*loaded);
                    ui::requestFrame();
                }
            }
        }
        else if (heroFade < 1.f)
        {
            heroFade = core::clamp01(heroFade + dt * 1.6f);
            ui::requestFrame();
        }

        const JobSnapshot snap = job.snapshot();
        if (snap.phase == JobPhase::Updating)
            meter.sample(snap.downloaded, dt);

        if (window.takeResized())
            device.resize(window.width(), window.height());
        if (window.takeScaleChanged())
        {
            ui::rebuildFonts(device.dev(), window.scale());
            buildIcons(device.dev(), window.scale());
        }

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
        constexpr std::array actionablePhases{JobPhase::Ready, JobPhase::UpdateReady};
        shell.startEnabled = std::ranges::contains(actionablePhases, snap.phase);
        shell.panelVisible = panelSlide > 0.f;
        const bool updatePending = snap.phase == JobPhase::UpdateReady;
        const bool installed = gameInstalled(settings, wf::Branch::Public);
        shell.startLabel = updatePending ? (installed ? "UPDATE" : "INSTALL") : "PLAY";
        shell.secondaryVisible = updatePending;
        // with no install to fall back to, the line offers to find one detection missed
        shell.secondaryLabel = installed ? "PLAY WITHOUT UPDATING" : "ALREADY INSTALLED? LOCATE IT";
        std::string statusBuffer;
        std::string detailBuffer;
        std::string readyBuffer;
        shell.languageIndex = languageIndexFromCode(settings.language);
        switch (snap.phase)
        {
        case JobPhase::Idle:
            shell.statusLine = checkLabel(snap);
            break;
        case JobPhase::Checking:
        {
            shell.statusLine = checkLabel(snap);
            if (snap.entryCount != 0)
            {
                shell.progress = core::clamp01(static_cast<float>(
                    static_cast<double>(snap.entryIndex) / static_cast<double>(snap.entryCount)));
                detailBuffer = std::format("{} / {}", snap.entryIndex, snap.entryCount);
                if (snap.hashedBytes != 0)
                    detailBuffer += std::format("  •  {} HASHED",
                                                core::formatBytes(snap.hashedBytes));
                shell.detailLine = detailBuffer;
            }
            break;
        }
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
        case JobPhase::UpdateReady:
            readyBuffer = std::format("UPDATE AVAILABLE  {} FILES  •  {}", snap.queuedFiles,
                                      core::formatBytes(snap.queuedBytes));
            shell.buildLabel = readyBuffer;
            break;
        case JobPhase::Ready:
            if (launchFailure.empty())
            {
                readyBuffer = std::format("{} IS UP TO DATE", railTitles[selectedTitle].label);
                shell.buildLabel = readyBuffer;
            }
            else
            {
                shell.buildLabel = launchFailure;
            }
            break;
        case JobPhase::Failed:
            shell.statusLine = snap.message ? std::string_view(*snap.message) : "UPDATE FAILED";
            break;
        case JobPhase::Cancelled:
            shell.statusLine = "CANCELLED";
            break;
        }
        const DefragSnapshot defragSnap = defrag.snapshot();
        if (defragSnap.running)
        {
            shell.startEnabled = false;
            shell.statusLine = "DEFRAGMENTING CACHE";
            detailBuffer.clear();
            if (defragSnap.total != 0)
            {
                shell.progress = core::clamp01(
                    static_cast<float>(static_cast<double>(defragSnap.processed)
                                       / static_cast<double>(defragSnap.total)));
                detailBuffer = std::format("{} / {}", core::formatBytes(defragSnap.processed),
                                           core::formatBytes(defragSnap.total));
            }
            if (defragSnap.currentFile)
            {
                if (!detailBuffer.empty())
                    detailBuffer += "  •  ";
                detailBuffer += *defragSnap.currentFile;
            }
            shell.detailLine = detailBuffer;
            ui::requestFrame();
        }
        else if (defragSnap.finished)
        {
            defrag.clearFinished();
            defrag.join();
            defragExit = defragSnap.exitCode;
            // an already-tidy cache finishes in seconds, so say so instead of flicking past
            defragNotice = 3.f;
            if (!options.wantShot)
            {
                meter.reset();
                job.restart();
            }
            ui::requestFrame();
        }
        else if (defragNotice > 0.f)
        {
            defragNotice -= dt;
            shell.progress = 0.f;
            shell.detailLine = {};
            shell.statusLine =
                defragExit == 0 ? "CACHE DEFRAGMENTED" : "THE DEFRAGMENTER REPORTED A FAILURE";
            ui::requestFrame();
        }
        gfx::Image* const baked = railTitles[static_cast<std::size_t>(selectedTitle)].hero;
        const HeroFrame heroFrame{baked != nullptr && baked->valid() ? baked : nullptr,
                                  liveHero.valid() ? &liveHero : nullptr, heroFade};
        drawShell(viewport, heroFrame, shell);
        const RailResult rail =
            drawRail(viewport, !shell.panelVisible, &publisherLogo, railTitles, selectedTitle,
                     shellFooterAxis());
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
        const bool locateRequested = shellSecondaryClicked() && !installed;
        const bool launchRequested =
            (shellSecondaryClicked() && installed) || (shellStartClicked() && !updatePending);
        if (locateRequested)
        {
            if (const auto folder = pickFolder(window.handle()))
            {
                if (settings.adoptInstallRoot(*folder))
                {
                    launchFailure.clear();
                    job.restart();
                }
                else
                {
                    launchFailure = "NO GAME FOUND IN THAT FOLDER";
                    core::error("{}", launchFailure);
                }
            }
            ui::requestFrame();
        }
        else if (shellStartClicked() && updatePending)
        {
            meter.reset();
            job.startUpdate();
            ui::requestFrame();
        }
        else if (launchRequested)
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
        if (rail.titleClicked >= 0)
            selectedTitle = rail.titleClicked;
        // the rail is launcher scope, the header is the selected title's: each opens its own tab
        if (shellCogClicked() || rail.cogClicked)
        {
            const PanelTab wanted = rail.cogClicked ? PanelTab::Launcher : PanelTab::Settings;
            panelOpen = !panelOpen || panel.tab != wanted;
            if (panelOpen)
            {
                panel.tab = wanted;
                panel.saveFailed = false;
                working = settings;
                panel.launcherLine =
                    std::format("LAUNCHER   {}", core::narrow(launcherVersion()));
                const auto build = gameBuildVersion(settings, wf::Branch::Public);
                panel.gameBuildLine =
                    build ? std::format("GAME BUILD   {}", core::narrow(*build)) : std::string();
            }
            closeDropdown();
            ui::requestFrame();
        }
        // outside the slide test: the walk must be reaped even if the panel is gone
        if (panel.staleRunning && !job.running())
        {
            panel.staleRunning = false;
            panel.staleLine = std::format("{} UNLISTED FILES, {}", job.staleFiles(),
                                          core::formatBytes(job.staleBytes()));
            // a stale walk leaves the job idle, not ready; a real check finds out which
            if (!options.wantShot)
            {
                meter.reset();
                job.restart();
            }
            ui::requestFrame();
        }
        if (panelSlide > 0.f)
        {
            const PanelTab beforeTab = panel.tab;
            const PanelAction panelAction =
                drawPanel(viewport, panelSlide, panel, working);
            if (panel.tab != beforeTab)
            {
                closeDropdown();
                ui::requestFrame();
            }
            switch (panelAction)
            {
            case PanelAction::Accept:
            {
                const bool needsRecheck = working.language != settings.language
                    || working.graphicsApi != settings.graphicsApi;
                if (working.save(&settings))
                {
                    settings = working;
                    panel.saveFailed = false;
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
                    panel.saveFailed = true;
                }
                ui::requestFrame();
                break;
            }
            case PanelAction::Dismiss:
                panelOpen = false;
                closeDropdown();
                ui::requestFrame();
                break;
            case PanelAction::Verify:
                panelOpen = false;
                if (!options.wantShot)
                {
                    meter.reset();
                    job.restart(true);
                }
                ui::requestFrame();
                break;
            case PanelAction::StaleReport:
                panel.staleLine.clear();
                if (!options.wantShot)
                {
                    panel.staleRunning = true;
                    job.startStaleReport();
                }
                ui::requestFrame();
                break;
            case PanelAction::Defragment:
                if (!options.wantShot)
                {
                    // the applet wants the disk to itself
                    job.cancel();
                    job.join();
                    if (const auto started = defrag.start(settings, wf::Branch::Public); started)
                    {
                        panelOpen = false;
                        panel.defragLine.clear();
                    }
                    else
                    {
                        panel.defragLine = core::narrow(describe(started.error()));
                        core::error("{}", panel.defragLine);
                    }
                }
                ui::requestFrame();
                break;
            case PanelAction::None:
                break;
            }
        }
        drawWindowControls(viewport);
        if (shellCloseClicked())
        {
            job.cancel();
            break;
        }
        if (shellMinimiseClicked())
            window.minimise();
        ui::endFrame();
        window.clearMouseEdge();
        renderer.render(device.ctx(), ui::dl(), device.width(), device.height());

        if (options.wantShot && elapsed >= options.shotTime)
        {
            if (!device.captureBackbuffer(options.shotPath))
                result = 1;
            break;
        }

        device.present(true);
        if (!ui::g().animated && !window.mouseDown())
            window.waitForInput();
    }

    job.cancel();
    job.join();
    heroArt.stop();

    liveHero = gfx::Image{};
    warframeHero = gfx::Image{};
    destroyIcons();
    ui::shutdown();
    renderer.destroy();
    device.destroy();
    window.destroy();
    gfx::shutdownImaging();
    ::CoUninitialize();
    return result;
}

}
