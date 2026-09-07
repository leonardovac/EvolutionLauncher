#include "app/app.h"

#include "app/assets.h"
#include "app/resource.h"
#include "app/shell.h"
#include "app/updatejob.h"
#include "app/window.h"
#include "core/log.h"
#include "core/types.h"
#include "gfx/device.h"
#include "gfx/image.h"
#include "gfx/renderer.h"
#include "ui/ui.h"

#include <objbase.h>

#include <chrono>

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
        static float logTimer = 0.f;
        logTimer += dt;
        if (logTimer >= 1.f)
        {
            logTimer = 0.f;
            core::info("job phase {} entry {}/{} bytes {}/{}", static_cast<int>(snap.phase),
                       snap.entryIndex, snap.entryCount, snap.downloaded, snap.downloadTotal);
        }

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
        ShellState shell;
        shell.statusLine = "UPDATING GAME  51%   250 / 1150 MB";
        shell.progress = 0.51f;
        shell.showProgress = true;
        shell.startEnabled = false;
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
