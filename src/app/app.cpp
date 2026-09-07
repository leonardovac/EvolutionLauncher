#include "app/app.h"

#include "app/window.h"
#include "core/types.h"
#include "gfx/device.h"
#include "gfx/drawlist.h"
#include "gfx/image.h"
#include "gfx/renderer.h"

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

    gfx::DrawList draw;
    auto previous = std::chrono::steady_clock::now();
    float elapsed = 0.f;

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
        draw.reset(viewport);
        draw.rect(viewport, core::Col::hex(0x0B0A0A, 1.f));

        renderer.render(device.ctx(), draw, device.width(), device.height());
        device.present(true);

        if (options.wantShot && elapsed >= options.shotTime)
        {
            device.captureBackbuffer(options.shotPath);
            break;
        }
    }

    renderer.destroy();
    device.destroy();
    window.destroy();
    gfx::shutdownImaging();
    ::CoUninitialize();
    return 0;
}

}
