#pragma once

#include "app/launch.h"
#include "core/win.h"
#include "update/plan.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace app
{

struct HandleTraits
{
    using Value = HANDLE;
    static Value invalid() noexcept { return nullptr; }
    static void close(Value value) noexcept { ::CloseHandle(value); }
};
using Handle = core::UniqueHandle<HandleTraits>;

// starts a game applet; a console it opens later is created hidden
std::expected<Handle, LaunchError> spawnHidden(std::wstring& line);

// the game has no console, but a sideloaded DLL may AllocConsole one into it
void hideAppletConsole(DWORD pid, HWND& console);

// the stock launcher's graceful stop: SoakStop once a second, then TerminateProcess after 30 tries
void stopApplet(HANDLE process);

// %LOCALAPPDATA%\<title>\<name>, where a -log:/<name> argument lands
std::wstring gameLogPath(wf::Title title, std::wstring_view name);

// the game truncates its log when it starts; until then everything past the opening size is a previous run
class LogTail
{
public:
    explicit LogTail(std::wstring path);

    template <class OnLine>
    void read(OnLine&& onLine)
    {
        if (path_.empty() || !fill())
            return;
        std::size_t start = 0;
        for (std::size_t newline = pending_.find('\n'); newline != std::string::npos; newline = pending_.find('\n', start))
        {
            onLine(std::string_view(pending_).substr(start, newline - start));
            start = newline + 1;
        }
        pending_.erase(0, start);
    }

private:
    bool fill();

    std::wstring path_;
    std::uint64_t offset_ = 0;
    std::string pending_;
};

}
