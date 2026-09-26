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
#include "app/theme.h"
#include "app/titles.h"
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
#include <span>
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

// empty on success, because a folder that holds the game needs no explaining
std::string_view describeProbe(RootProbe probe)
{
    switch (probe)
    {
    case RootProbe::HasGame: return {};
    case RootProbe::Empty: return "NO GAME FOUND HERE - IT WILL BE INSTALLED INTO THIS FOLDER";
    case RootProbe::Occupied:
        return "THAT FOLDER HOLDS OTHER FILES - PICK THE GAME'S OR AN EMPTY ONE";
    case RootProbe::Unusable: return "THAT FOLDER CANNOT BE READ";
    }
    return {};
}

bool adoptedRoot(RootProbe probe)
{
    constexpr std::array accepted{RootProbe::HasGame, RootProbe::Empty};
    return std::ranges::contains(accepted, probe);
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

    gfx::Image publisherLogo;
    std::array<gfx::Image, 2> heroes;
    std::array<gfx::Image, 2> icons;
    std::array<gfx::Image, 2> leaves;
    std::array<gfx::Image, 2> endCaps;
    const auto loadArt = [&device](int id, gfx::Image& out) {
        if (const auto bytes = resource(id); !bytes.empty())
        {
            if (auto loaded = gfx::loadImageMemory(device.dev(), bytes))
                out = std::move(*loaded);
        }
    };
    loadArt(RES_PUBLISHER_LOGO, publisherLogo);

    std::array<RailTitle, 2> railTitles{};
    for (std::size_t i = 0; i < railTitles.size(); ++i)
    {
        const TitleProfile& entry = titleProfiles()[i];
        loadArt(entry.heroResource, heroes[i]);
        loadArt(entry.iconResource, icons[i]);
        if (entry.leafResource != 0)
            loadArt(entry.leafResource, leaves[i]);
        if (entry.endCapResource != 0)
            loadArt(entry.endCapResource, endCaps[i]);
        railTitles[i] = RailTitle{entry.railId, entry.label, &icons[i], &heroes[i]};
    }
    const wf::LauncherConfig launcherConfig = wf::LauncherConfig::load();
    int selectedTitle = 0;
    const auto selectTitle = [&selectedTitle](wf::Title wanted) {
        for (std::size_t i = 0; i < titleProfiles().size(); ++i)
        {
            if (titleProfiles()[i].title == wanted)
                selectedTitle = static_cast<int>(i);
        }
    };
    if (const auto remembered = launcherConfig.lastTitle())
    {
        if (const auto parsed = parseTitleName(*remembered))
            selectTitle(*parsed);
    }
    // an explicit capture flag outranks what was remembered
    if (options.shotTitle)
        selectTitle(*options.shotTitle);

    const auto currentTitle = [&selectedTitle]() {
        return titleProfiles()[static_cast<std::size_t>(selectedTitle)].title;
    };
    setTitleTheme(currentTitle());

    UpdateJob job;
    job.setTitle(currentTitle());
    if (!options.wantShot)
        job.start();
    RateMeter meter;

    Settings settings = Settings::load(currentTitle(), launcherConfig);
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
    const auto openPanel = [&panel, &working, &settings](PanelTab tab) {
        panel.tab = tab;
        panel.saveFailed = false;
        panel.rootLine.clear();
        panel.rootPick.clear();
        working = settings;
        panel.launcherLine = std::format("LAUNCHER   {}", core::narrow(launcherVersion()));
        const auto build = gameBuildVersion(settings, wf::Branch::Public);
        panel.gameBuildLine = build ? std::format("GAME BUILD   {}", core::narrow(*build)) : std::string();
    };

    DefragJob defrag;
    float defragNotice = 0.f;
    std::uint32_t defragExit = 0;

    HeroArt heroArt;
    gfx::Image liveHero;
    gfx::Image nextHero;
    float heroFade = 0.f;
    const auto loadHero = [&device](std::span<const std::uint8_t> bytes, gfx::Image& out) {
        if (bytes.empty())
            return false;
        auto loaded = gfx::loadImageMemory(device.dev(), bytes);
        if (!loaded)
            return false;
        out = std::move(*loaded);
        return true;
    };
    // a screenshot keeps the baked art to stay reproducible
    if (!options.wantShot)
        loadHero(heroArt.begin(currentTitle()), liveHero);


    window.setTitle(core::widen(railTitles[static_cast<std::size_t>(selectedTitle)].label));

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

        // warm art is already settled, so anything the scrape hands over is newer and fades over it
        if (const auto art = heroArt.take(); !art.empty() && loadHero(art, nextHero))
        {
            heroFade = 0.f;
            ui::requestFrame();
        }
        if (nextHero.valid())
        {
            heroFade = core::clamp01(heroFade + dt * 1.6f);
            if (heroFade >= 1.f)
            {
                liveHero = std::move(nextHero);
                nextHero = gfx::Image{};
            }
            ui::requestFrame();
        }

        const JobSnapshot snap = job.snapshot();
        constexpr std::array downloadPhases{JobPhase::Updating, JobPhase::UpdatingContent};
        if (std::ranges::contains(downloadPhases, snap.phase))
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
        input.keys = window.keys();
        ui::newFrame(input, dt, static_cast<float>(device.width()),
                     static_cast<float>(device.height()));
        constexpr std::array liveJobPhases{JobPhase::Checking, JobPhase::Updating, JobPhase::UpdatingContent};
        if (std::ranges::contains(liveJobPhases, snap.phase))
            ui::requestFrame();
        ShellState shell;
        shell.phase = snap.phase;
        constexpr std::array actionablePhases{JobPhase::Ready, JobPhase::UpdateReady};
        shell.startEnabled = std::ranges::contains(actionablePhases, snap.phase);
        shell.panelVisible = panelSlide > 0.f;
        shell.nav = titleProfiles()[static_cast<std::size_t>(selectedTitle)].nav;
        gfx::Image& leafArt = leaves[static_cast<std::size_t>(selectedTitle)];
        shell.leaf = leafArt.valid() ? &leafArt : nullptr;
        gfx::Image& capArt = endCaps[static_cast<std::size_t>(selectedTitle)];
        shell.endCap = capArt.valid() ? &capArt : nullptr;
        const bool updatePending = snap.phase == JobPhase::UpdateReady;
        const bool installed = gameInstalled(settings, wf::Branch::Public);
        shell.startLabel = updatePending
            ? (installed ? "UPDATE" : "INSTALL")
            : titleProfiles()[static_cast<std::size_t>(selectedTitle)].playLabel;
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
        case JobPhase::UpdatingContent:
        {
            // the applet names no total until it has scanned the caches, so sweep until then
            shell.phase = snap.downloadTotal != 0 ? JobPhase::Updating : JobPhase::Checking;
            shell.statusLine = "CHECKING GAME CONTENT";
            if (snap.downloadTotal == 0)
                break;
            shell.progress = core::clamp01(static_cast<float>(static_cast<double>(snap.downloaded) / static_cast<double>(snap.downloadTotal)));
            statusBuffer = std::format("UPDATING GAME CONTENT  {}%", static_cast<int>(shell.progress * 100.f));
            shell.statusLine = statusBuffer;
            detailBuffer = std::format("{} / {}", core::formatBytes(snap.downloaded), core::formatBytes(snap.downloadTotal));
            if (meter.ready())
            {
                if (const std::string rate = formatRate(meter.bytesPerSecond()); !rate.empty())
                    detailBuffer += std::format("  •  {}", rate);
                const std::uint64_t remaining = snap.downloadTotal > snap.downloaded ? snap.downloadTotal - snap.downloaded : 0u;
                if (const std::string eta = formatEta(remaining, meter.bytesPerSecond()); !eta.empty())
                    detailBuffer += std::format("  •  {}", eta);
            }
            shell.detailLine = detailBuffer;
            break;
        }
        case JobPhase::UpdateReady:
            readyBuffer = std::format("UPDATE AVAILABLE  {} FILES  •  {}", snap.queuedFiles,
                                      core::formatBytes(snap.queuedBytes));
            shell.buildLabel = readyBuffer;
            shell.buildLabelLink = snap.queuedRows != nullptr;
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
            // the shell draws the bar only in its busy phases, whatever the update job is doing
            shell.phase = defragSnap.total != 0 ? JobPhase::Updating : JobPhase::Checking;
            shell.statusLine = "DEFRAGMENTING CACHE";
            detailBuffer.clear();
            if (defragSnap.total != 0)
            {
                shell.progress = core::clamp01(
                    static_cast<float>(static_cast<double>(defragSnap.processed)
                                       / static_cast<double>(defragSnap.total)));
                statusBuffer = std::format("DEFRAGMENTING CACHE  {}%", static_cast<int>(shell.progress * 100.f));
                shell.statusLine = statusBuffer;
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
            shell.buildLabel = shell.statusLine;
            shell.buildLabelLink = false;
            ui::requestFrame();
        }
        gfx::Image* baked = railTitles[static_cast<std::size_t>(selectedTitle)].hero;
        if (baked == nullptr || !baked->valid())
            baked = nullptr;
        const HeroFrame heroFrame{liveHero.valid() ? &liveHero : baked,
                                  nextHero.valid() ? &nextHero : nullptr, heroFade};
        drawShell(viewport, heroFrame, shell);
        const RailResult rail =
            drawRail(viewport, !shell.panelVisible, &publisherLogo, railTitles,
                     selectedTitle);
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
            if (const auto folder = pickFolder(window.handle(), settings.installRoot(wf::Branch::Public)))
            {
                Settings next = settings;
                const RootProbe probe = next.adoptInstallRoot(*folder);
                launchFailure = std::string(describeProbe(probe));
                if (adoptedRoot(probe) && next.save(&settings))
                {
                    settings = next;
                    job.restart();
                }
                else if (launchFailure.empty())
                {
                    launchFailure = "COULD NOT RECORD THAT FOLDER";
                }
                if (!launchFailure.empty())
                    core::error("{}", launchFailure);
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
        // a switch re-points everything: the job, the settings, the art and the caption
        if (rail.titleClicked >= 0 && rail.titleClicked != selectedTitle
            && !defrag.snapshot().running)
        {
            job.cancel();
            job.join();
            selectedTitle = rail.titleClicked;
            setTitleTheme(currentTitle());
            wf::LauncherConfig stored = wf::LauncherConfig::load();
            stored.setLastTitle(profile(currentTitle()).localFolder);
            if (!stored.save())
                core::error("could not record the selected title");
            settings = Settings::load(currentTitle(), wf::LauncherConfig::load());
            launchFailure.clear();
            meter.reset();
            liveHero = gfx::Image{};
            nextHero = gfx::Image{};
            heroFade = 0.f;
            heroArt.stop();
            panelOpen = false;
            closeDropdown();
            window.setTitle(core::widen(railTitles[static_cast<std::size_t>(selectedTitle)].label));
            job.setTitle(currentTitle());
            if (!options.wantShot)
            {
                job.restart();
                loadHero(heroArt.begin(currentTitle()), liveHero);
            }
            ui::requestFrame();
        }
        // the rail is launcher scope, the header is the selected title's: each opens its own tab
        if (shellCogClicked() || rail.cogClicked)
        {
            const PanelTab wanted = rail.cogClicked ? PanelTab::Launcher : PanelTab::Settings;
            panelOpen = !panelOpen || panel.tab != wanted;
            if (panelOpen)
                openPanel(wanted);
            closeDropdown();
            ui::requestFrame();
        }
        if (shellBuildLabelClicked() && snap.queuedRows)
        {
            openPanel(PanelTab::Files);
            panelOpen = true;
            panel.files = snap.queuedRows;
            panel.filesLine = std::format("{} FILES  •  {}", snap.queuedFiles, core::formatBytes(snap.queuedBytes));
            panel.filesScroll = 0.f;
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
                drawPanel(viewport, panelSlide, panel, working, window.wheel());
            if (panel.tab != beforeTab)
            {
                closeDropdown();
                ui::requestFrame();
            }
            switch (panelAction)
            {
            case PanelAction::Accept:
            {
                // sideload changes what counts as up to date, so the plan has to be rebuilt
                const bool needsRecheck = working.language != settings.language
                    || working.graphicsApi != settings.graphicsApi
                    || working.sideload != settings.sideload
                    || working.installRootOverride != settings.installRootOverride;
                if (working.save(&settings))
                {
                    settings = working;
                    panel.saveFailed = false;
                    panel.rootLine.clear();
                    panel.rootPick.clear();
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
                panel.rootLine.clear();
                panel.rootPick.clear();
                ui::requestFrame();
                break;
            case PanelAction::LocateRoot:
                if (const auto folder = pickFolder(window.handle(), working.installRoot(wf::Branch::Public)))
                {
                    const RootProbe probe = working.adoptInstallRoot(*folder);
                    panel.rootLine = std::string(describeProbe(probe));
                    // a refusal leaves the root line unchanged, so name the folder it turned down
                    panel.rootPick = adoptedRoot(probe) ? std::string() : core::narrow(folder->wstring());
                }
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
        window.clearInputEdge();
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
    for (gfx::Image& image : heroes)
        image = gfx::Image{};
    for (gfx::Image& image : icons)
        image = gfx::Image{};
    for (gfx::Image& image : leaves)
        image = gfx::Image{};
    for (gfx::Image& image : endCaps)
        image = gfx::Image{};
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
