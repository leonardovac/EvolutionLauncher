#include "app/defragjob.h"

#include "core/log.h"
#include "core/str.h"
#include "core/win.h"

#include <windows.h>

#include <array>
#include <string_view>

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

// the game is a GUI binary that calls AllocConsole, so its console is a window of its own
constexpr std::wstring_view consoleClass = L"ConsoleWindowClass";
// SetConsoleTitleW writes "Defrag <done>/<total> <n>% complete"; the log never carries it
constexpr std::wstring_view titleMark = L"Defrag ";

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

std::uint64_t readNumber(std::wstring_view text, std::size_t& at)
{
    std::uint64_t value = 0;
    while (at < text.size() && text[at] >= L'0' && text[at] <= L'9')
        value = value * 10 + static_cast<std::uint64_t>(text[at++] - L'0');
    return value;
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
    thread_ = std::thread(&DefragJob::pump, this, info.hProcess);
    return {};
}

void DefragJob::pump(void* process)
{
    const Handle child(static_cast<HANDLE>(process));
    const DWORD pid = ::GetProcessId(child.get());
    HWND console = nullptr;

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

        if (console != nullptr)
        {
            std::array<wchar_t, 256> title{};
            if (::GetWindowTextW(console, title.data(), static_cast<int>(title.size())) > 0)
            {
                const std::wstring_view text(title.data());
                if (const std::size_t at = text.find(titleMark); at != std::wstring_view::npos)
                {
                    std::size_t cursor = at + titleMark.size();
                    const std::uint64_t done = readNumber(text, cursor);
                    if (cursor < text.size() && text[cursor] == L'/')
                    {
                        ++cursor;
                        const std::uint64_t all = readNumber(text, cursor);
                        if (all != 0)
                        {
                            processed_.store(done, std::memory_order_relaxed);
                            total_.store(all, std::memory_order_relaxed);
                        }
                    }
                }
            }
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
