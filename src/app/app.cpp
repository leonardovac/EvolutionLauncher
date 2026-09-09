#include "app/app.h"

#include "app/assets.h"
#include "app/resource.h"
#include "app/shell.h"
#include "app/updatejob.h"
#include "app/window.h"
#include "core/str.h"
#include "core/types.h"
#include "gfx/device.h"
#include "gfx/image.h"
#include "gfx/renderer.h"
#include "ui/ui.h"

#include <objbase.h>

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

    const auto fontBytes = resource(WF_RES_FONT);
    ui::FontData fontData;
    fontData.semiBold = fontBytes.data();
    fontData.semiBoldSize = fontBytes.size();
    fontData.bold = fontBytes.data();
    fontData.boldSize = fontBytes.size();
    if (!ui::init(device.dev(), fontData))
        return 1;
    ui::rebuildFonts(device.dev(), window.scale());

    gfx::Image hero;
    if (const auto bytes = resource(WF_RES_HERO); !bytes.empty())
    {
        if (auto loaded = gfx::loadImageMemory(device.dev(), bytes))
            hero = std::move(*loaded);
    }

    UpdateJob job;
    if (!options.wantShot)
        job.start();

    auto previous = std::chrono::steady_clock::now();
    float elapsed = 0.f;
    int result = 0;

    while (window.pump())
    {
        const auto now = std::chrono::steady_clock::now();
        const float dt = std::chrono::duration<float>(now - previous).count();
        previous = now;
        elapsed += dt;

        const JobSnapshot snap = job.snapshot();

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
        constexpr std::array terminalPhases{JobPhase::Ready, JobPhase::Failed,
                                            JobPhase::Cancelled};
        ShellState shell;
        shell.phase = snap.phase;
        shell.startEnabled = std::ranges::contains(terminalPhases, snap.phase);
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
            shell.statusLine = std::format("UPDATING GAME  {}%   {} / {}",
                                           static_cast<int>(fraction * 100.f),
                                           core::formatBytes(snap.downloaded),
                                           core::formatBytes(snap.downloadTotal));
            break;
        }
        case JobPhase::Ready:
            shell.buildLabel = "READY";
            break;
        case JobPhase::Failed:
            shell.statusLine = snap.message.empty() ? "UPDATE FAILED" : snap.message;
            break;
        case JobPhase::Cancelled:
            shell.statusLine = "CANCELLED";
            break;
        }
        drawShell(viewport, hero.valid() ? &hero : nullptr, shell);
        if (shellCloseClicked())
            break;
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
    }

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
