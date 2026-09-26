#include "app/defragjob.h"

#include "core/log.h"
#include "core/str.h"

#include <windows.h>

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <memory>
#include <string_view>
#include <system_error>
#include <vector>

namespace app
{
namespace
{

// EE.log lines: "Defragged <done>/<total>" in bytes, "Defragmenting /Cache.Windows/<file>"
constexpr std::string_view progressMark = "Defragged ";
constexpr std::string_view fileMark = "Defragmenting ";

struct TmpFile
{
    std::string name;
    std::uint64_t size = 0;
};

// every figure here is at most the real progress, so their sum never runs ahead of it
struct DefragProgress
{
    std::uint64_t logDone = 0;
    std::uint64_t logTotal = 0;
    std::string logFile;
    // logDone when logFile was named: every file before it, at most
    std::uint64_t logFileBase = 0;
    // files the log has named; logDone already counts each of them in full or in part
    std::vector<std::string> logged;
    std::vector<TmpFile> finished;
    std::vector<TmpFile> writing;
};

void parseLine(std::string_view line, DefragProgress& progress)
{
    if (const std::size_t at = line.find(progressMark); at != std::string_view::npos)
    {
        const char* last = line.data() + line.size();
        std::uint64_t done = 0;
        std::uint64_t all = 0;
        const auto [slash, parsed] = std::from_chars(line.data() + at + progressMark.size(), last, done);
        if (parsed != std::errc{} || slash == last || *slash != '/')
            return;
        if (std::from_chars(slash + 1, last, all).ec != std::errc{} || all == 0)
            return;
        progress.logDone = done;
        progress.logTotal = all;
        return;
    }
    if (const std::size_t at = line.find(fileMark); at != std::string_view::npos)
    {
        std::string_view path = line.substr(at + fileMark.size());
        while (!path.empty() && path.back() == '\r')
            path.remove_suffix(1);
        if (const std::size_t slash = path.rfind('/'); slash != std::string_view::npos)
            path.remove_prefix(slash + 1);
        progress.logFile = path;
        progress.logFileBase = progress.logDone;
        if (!std::ranges::contains(progress.logged, progress.logFile))
            progress.logged.push_back(progress.logFile);
    }
}

std::uint64_t cacheBytes(const std::filesystem::path& dir)
{
    std::uint64_t bytes = 0;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
    {
        std::error_code sizeError;
        const std::uint64_t size = it->file_size(sizeError);
        if (it->path().extension() == L".cache" && !sizeError)
            bytes += size;
    }
    return bytes;
}

std::vector<TmpFile> listWriting(const std::filesystem::path& dir)
{
    constexpr std::wstring_view tmpSuffix = L".tmp";
    std::vector<TmpFile> out;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
    {
        const std::wstring name = it->path().filename().wstring();
        if (!name.ends_with(L".cache.tmp"))
            continue;
        std::error_code sizeError;
        const std::uint64_t size = it->file_size(sizeError);
        out.push_back({core::narrow(std::wstring_view(name).substr(0, name.size() - tmpSuffix.size())), sizeError ? 0 : size});
    }
    return out;
}

void trackWriting(DefragProgress& progress, const std::filesystem::path& dir)
{
    std::vector<TmpFile> now = listWriting(dir);
    for (TmpFile& was : progress.writing)
    {
        if (std::ranges::contains(now, was.name, &TmpFile::name))
            continue;
        // a .tmp that is gone was moved into place, so the file now holds its last bytes
        std::error_code ec;
        const std::uint64_t moved = std::filesystem::file_size(dir / core::widen(was.name), ec);
        if (!ec)
            was.size = std::max(was.size, moved);
        progress.finished.push_back(std::move(was));
    }
    progress.writing = std::move(now);
}

std::uint64_t estimateDone(const DefragProgress& progress)
{
    std::uint64_t unlogged = 0;
    std::uint64_t current = 0;
    const auto count = [&](const TmpFile& file) {
        if (file.name == progress.logFile)
            current = std::max(current, file.size);
        else if (!std::ranges::contains(progress.logged, file.name))
            unlogged += file.size;
    };
    std::ranges::for_each(progress.finished, count);
    std::ranges::for_each(progress.writing, count);
    return unlogged + std::max(progress.logDone, progress.logFileBase + current);
}

}

DefragJob::~DefragJob()
{
    join();
}

bool DefragJob::running() const noexcept
{
    return running_.load(std::memory_order_acquire);
}

void DefragJob::clearFinished() noexcept
{
    finished_.store(false, std::memory_order_relaxed);
}

DefragSnapshot DefragJob::snapshot() const
{
    DefragSnapshot out;
    out.running = running_.load(std::memory_order_acquire);
    out.finished = finished_.load(std::memory_order_acquire);
    out.currentFile = currentFile_.load(std::memory_order_acquire);
    out.processed = processed_.load(std::memory_order_relaxed);
    out.total = total_.load(std::memory_order_relaxed);
    out.exitCode = exitCode_.load(std::memory_order_relaxed);
    return out;
}

void DefragJob::join()
{
    if (thread_.joinable())
        thread_.join();
}

std::expected<void, LaunchError> DefragJob::start(const Settings& settings, wf::Branch branch)
{
    if (running())
        return {};

    auto line = buildDefragCommandLine(settings, branch);
    if (!line)
        return std::unexpected(line.error());

    core::info("defragmenting: {}", core::narrow(*line));

    LogTail log(gameLogPath(settings.title, L"EE.log"));
    std::wstring cacheDir = (settings.installRoot(branch) / L"Cache.Windows").wstring();

    auto child = spawnHidden(*line);
    if (!child)
        return std::unexpected(child.error());

    processed_.store(0, std::memory_order_relaxed);
    total_.store(0, std::memory_order_relaxed);
    exitCode_.store(0, std::memory_order_relaxed);
    currentFile_.store({}, std::memory_order_release);
    finished_.store(false, std::memory_order_relaxed);
    running_.store(true, std::memory_order_release);

    join();
    thread_ = std::thread(&DefragJob::pump, this, std::move(*child), std::move(log), std::move(cacheDir));
    return {};
}

void DefragJob::pump(Handle child, LogTail log, std::wstring cacheDir)
{
    const DWORD pid = ::GetProcessId(child.get());
    HWND console = nullptr;
    DefragProgress progress;
    const std::filesystem::path cache(cacheDir);
    // the defragmenter's own total skips a few blocks; this stands in until the log names it
    const std::uint64_t roughTotal = cacheBytes(cache);
    std::uint64_t shownDone = 0;
    std::string shownFile;

    for (;;)
    {
        const DWORD waited = ::WaitForSingleObject(child.get(), 200);

        hideAppletConsole(pid, console);
        log.read([&progress](std::string_view line) { parseLine(line, progress); });
        trackWriting(progress, cache);

        const std::uint64_t total = progress.logTotal != 0 ? progress.logTotal : roughTotal;
        shownDone = std::min(std::max(shownDone, estimateDone(progress)), total);
        processed_.store(shownDone, std::memory_order_relaxed);
        total_.store(total, std::memory_order_relaxed);
        const std::string& file = progress.writing.empty() ? progress.logFile : progress.writing.front().name;
        if (file != shownFile)
        {
            shownFile = file;
            currentFile_.store(std::make_shared<const std::string>(shownFile), std::memory_order_release);
        }

        if (waited == WAIT_OBJECT_0)
            break;
    }

    DWORD code = 0;
    ::GetExitCodeProcess(child.get(), &code);
    exitCode_.store(static_cast<std::uint32_t>(code), std::memory_order_relaxed);
    core::info("defragmenter exited with {}", code);
    finished_.store(true, std::memory_order_release);
    running_.store(false, std::memory_order_release);
}

}
