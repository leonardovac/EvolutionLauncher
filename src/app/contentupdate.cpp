#include "app/contentupdate.h"

#include "app/applet.h"
#include "core/log.h"
#include "core/str.h"

#include <windows.h>

#include <charconv>
#include <string_view>
#include <system_error>

namespace app
{
namespace
{

// Preprocess.log lines: "Downloaded <done>/<total>" in bytes, and the completion line stock requires
constexpr std::string_view progressMark = "Downloaded ";
constexpr std::string_view completeMark = "All stripped assets preprocessed";

struct ContentProgress
{
    std::uint64_t downloaded = 0;
    std::uint64_t total = 0;
    bool complete = false;
};

void parseLine(std::string_view line, ContentProgress& progress)
{
    while (!line.empty() && line.back() == '\r')
        line.remove_suffix(1);
    if (line.contains(completeMark))
    {
        progress.complete = true;
        return;
    }
    const std::size_t at = line.find(progressMark);
    if (at == std::string_view::npos)
        return;
    const char* last = line.data() + line.size();
    std::uint64_t done = 0;
    std::uint64_t all = 0;
    const auto [slash, parsed] = std::from_chars(line.data() + at + progressMark.size(), last, done);
    if (parsed != std::errc{} || slash == last || *slash != '/')
        return;
    const auto [end, parsedAll] = std::from_chars(slash + 1, last, all);
    if (parsedAll != std::errc{} || end != last)
        return;
    progress.downloaded = done;
    progress.total = all;
}

}

std::expected<ContentUpdateResult, LaunchError> runContentUpdate(const Settings& settings, wf::Branch branch, const core::CancelToken& cancel, const std::function<void(std::uint64_t downloaded, std::uint64_t total)>& onProgress)
{
    auto line = buildContentUpdateCommandLine(settings, branch);
    if (!line)
        return std::unexpected(line.error());

    core::info("updating content: {}", core::narrow(*line));

    LogTail log(gameLogPath(settings.title, contentUpdateLog));
    const auto child = spawnHidden(*line);
    if (!child)
        return std::unexpected(child.error());

    const DWORD pid = ::GetProcessId(child->get());
    HWND console = nullptr;
    ContentProgress progress;
    bool stopping = false;

    for (;;)
    {
        const DWORD waited = ::WaitForSingleObject(child->get(), 200);

        hideAppletConsole(pid, console);
        log.read([&progress](std::string_view text) { parseLine(text, progress); });
        onProgress(progress.downloaded, progress.total);

        if (waited == WAIT_OBJECT_0)
            break;
        if (cancel.requested() && !stopping)
        {
            stopping = true;
            stopApplet(child->get());
        }
    }

    DWORD code = 0;
    ::GetExitCodeProcess(child->get(), &code);
    core::info("content update exited with {}{}", code, progress.complete ? "" : ", incomplete");
    return ContentUpdateResult{static_cast<std::uint32_t>(code), progress.complete};
}

}
