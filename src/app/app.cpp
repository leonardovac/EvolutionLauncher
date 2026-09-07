#include "app/app.h"

#include "app/assets.h"
#include "app/resource.h"
#include "app/window.h"
#include "core/types.h"
#include "gfx/device.h"
#include "gfx/drawlist.h"
#include "gfx/image.h"
#include "gfx/renderer.h"
#include "ui/ui.h"
#include "ui/widgets.h"

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

    auto previous = std::chrono::steady_clock::now();
    float elapsed = 0.f;
    int result = 0;

    while (window.pump())
    {
        const auto now = std::chrono::steady_clock::now();
        const float dt = std::chrono::duration<float>(now - previous).count();
        previous = now;
        elapsed += dt;

        if (window.takeResized())
            device.resize(window.width(), window.height());

        device.waitForFrame();
        device.beginFrame();

        const core::Rect viewport(0.f, 0.f, static_cast<float>(device.width()),
                                  static_cast<float>(device.height()));

        ui::Input input;
        ui::newFrame(input, dt, static_cast<float>(device.width()),
                     static_cast<float>(device.height()));
        ui::dl().rect(viewport, ui::theme().body);
        ui::heroCard(viewport, hero.valid() ? &hero : nullptr, ui::theme().focus, 0.f, 1.f);
        ui::heroOverlay(viewport, 0.f, 0.35f);
        ui::text(ui::fonts().title, core::Rect(40.f, 40.f, 600.f, 80.f), "WARFRAME",
                 ui::theme().text, ui::AlignH::Left, ui::AlignV::Middle, 4.f);
        ui::endFrame();
        renderer.render(device.ctx(), ui::dl(), device.width(), device.height());
        device.present(true);

        if (options.wantShot && elapsed >= options.shotTime)
        {
            if (!device.captureBackbuffer(options.shotPath))
                result = 1;
            break;
        }
    }

    renderer.destroy();
    device.destroy();
    window.destroy();
    ui::shutdown();
    gfx::shutdownImaging();
    ::CoUninitialize();
    return result;
}

}
