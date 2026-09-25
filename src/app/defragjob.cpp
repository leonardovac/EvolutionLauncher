#include "app/defragjob.h"

#include "app/titles.h"
#include "core/log.h"
#include "core/str.h"
#include "core/win.h"

#include <windows.h>

#include <algorithm>
#include <array>
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

struct HandleTraits
{
    using Value = HANDLE;
    static Value invalid() noexcept { return nullptr; }
    static void close(Value value) noexcept { ::CloseHandle(value); }
};
using Handle = core::UniqueHandle<HandleTraits>;

// the game has no console, but a sideloaded DLL may AllocConsole one into it
constexpr std::wstring_view consoleClass = L"ConsoleWindowClass";
// EE.log lines: "Defragged <done>/<total>" in bytes, "Defragmenting /Cache.Windows/<file>"
constexpr std::string_view progressMark = "Defragged ";
constexpr std::string_view fileMark = "Defragmenting ";

struct ConsoleSearch
{
    DWORD pid = 0;
    HWND found = nullptr;
};

BOOL CALLBACK findConsole(HWND window, LPARAM param)
{
    auto* search = reinterpret_cast<ConsoleSearch*>(param);
    DWORD owner = 0;
    ::GetWindowThreadProcessId(window, &owner);
    if (owner != search->pid)
        return TRUE;
    std::array<wchar_t, 64> name{};
    ::GetClassNameW(window, name.data(), static_cast<int>(name.size()));
    if (std::wstring_view(name.data()) != consoleClass)
        return TRUE;
    search->found = window;
    return FALSE;
}

std::wstring gameLogPath(wf::Title title)
{
    std::array<wchar_t, MAX_PATH> local{};
    const DWORD written = ::GetEnvironmentVariableW(L"LOCALAPPDATA", local.data(), static_cast<DWORD>(local.size()));
    if (written == 0 || written >= local.size())
        return {};
    return (std::filesystem::path(local.data()) / profile(title).localFolder / L"EE.log").wstring();
}

std::uint64_t fileSize(const std::wstring& path)
{
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (::GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data) == FALSE)
        return 0;
    return (static_cast<std::uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
}

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

void readLog(const std::wstring& log, std::uint64_t& offset, std::string& pending, DefragProgress& progress)
{
    const core::File file(::CreateFileW(log.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file)
        return;
    LARGE_INTEGER size{};
    if (::GetFileSizeEx(file.get(), &size) == FALSE)
        return;
    const auto end = static_cast<std::uint64_t>(size.QuadPart);
    if (end < offset)
    {
        offset = 0;
        pending.clear();
    }
    LARGE_INTEGER seek{};
    seek.QuadPart = static_cast<LONGLONG>(offset);
    if (::SetFilePointerEx(file.get(), seek, nullptr, FILE_BEGIN) == FALSE)
        return;

    std::array<char, 16 * 1024> chunk{};
    while (offset < end)
    {
        const auto want = static_cast<DWORD>(std::min<std::uint64_t>(chunk.size(), end - offset));
        DWORD got = 0;
        if (::ReadFile(file.get(), chunk.data(), want, &got, nullptr) == FALSE || got == 0)
            break;
        offset += got;
        pending.append(chunk.data(), got);
    }

    std::size_t start = 0;
    for (std::size_t newline = pending.find('\n'); newline != std::string::npos; newline = pending.find('\n', start))
    {
        parseLine(std::string_view(pending).substr(start, newline - start), progress);
        start = newline + 1;
    }
    pending.erase(0, start);
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

    std::wstring log = gameLogPath(settings.title);
    // the game truncates EE.log when it starts; until then everything past here is a previous run
    const std::uint64_t offset = fileSize(log);
    std::wstring cacheDir = (settings.installRoot(branch) / L"Cache.Windows").wstring();

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    // AllocConsole gives the new console this show state, so it never appears
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION info{};
    const BOOL ok = ::CreateProcessW(nullptr, line->data(), nullptr, nullptr, FALSE,
                                     CREATE_UNICODE_ENVIRONMENT | NORMAL_PRIORITY_CLASS, nullptr,
                                     nullptr, &startup, &info);
    if (ok == FALSE)
        return std::unexpected(LaunchError::SpawnFailed);

    const Handle spawnedThread(info.hThread);

    processed_.store(0, std::memory_order_relaxed);
    total_.store(0, std::memory_order_relaxed);
    exitCode_.store(0, std::memory_order_relaxed);
    currentFile_.store({}, std::memory_order_release);
    finished_.store(false, std::memory_order_relaxed);
    running_.store(true, std::memory_order_release);

    join();
    thread_ = std::thread(&DefragJob::pump, this, info.hProcess, std::move(log), offset, std::move(cacheDir));
    return {};
}

void DefragJob::pump(void* process, std::wstring log, std::uint64_t offset, std::wstring cacheDir)
{
    const Handle child(static_cast<HANDLE>(process));
    const DWORD pid = ::GetProcessId(child.get());
    HWND console = nullptr;
    std::string pending;
    DefragProgress progress;
    const std::filesystem::path cache(cacheDir);
    // the defragmenter's own total skips a few blocks; this stands in until the log names it
    const std::uint64_t roughTotal = cacheBytes(cache);
    std::uint64_t shownDone = 0;
    std::string shownFile;

    for (;;)
    {
        const DWORD waited = ::WaitForSingleObject(child.get(), 200);

        if (console == nullptr)
        {
            ConsoleSearch search{pid, nullptr};
            ::EnumWindows(findConsole, reinterpret_cast<LPARAM>(&search));
            console = search.found;
            // a hidden show state is not guaranteed, so close the gap if one slipped through
            if (console != nullptr && ::IsWindowVisible(console) != FALSE)
                ::ShowWindow(console, SW_HIDE);
        }

        if (!log.empty())
            readLog(log, offset, pending, progress);
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
