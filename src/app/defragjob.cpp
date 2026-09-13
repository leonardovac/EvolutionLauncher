#include "app/defragjob.h"

#include "core/log.h"
#include "core/str.h"
#include "core/win.h"

#include <windows.h>

#include <array>
#include <charconv>
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

constexpr std::string_view progressMark = "Defragged ";
constexpr std::string_view fileMark = "Defragmenting ";

std::uint64_t parseNumber(std::string_view text)
{
    std::uint64_t value = 0;
    std::from_chars(text.data(), text.data() + text.size(), value);
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

    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = TRUE;

    HANDLE readEnd = nullptr;
    HANDLE writeEnd = nullptr;
    if (::CreatePipe(&readEnd, &writeEnd, &attributes, 0) == FALSE)
        return std::unexpected(LaunchError::SpawnFailed);
    Handle readPipe(readEnd);
    Handle writePipe(writeEnd);
    // the child must not hold the read end open or the reader never sees end of file
    ::SetHandleInformation(readPipe.get(), HANDLE_FLAG_INHERIT, 0);

    core::info("defragmenting: {}", core::narrow(*line));

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = writePipe.get();
    startup.hStdError = writePipe.get();
    startup.hStdInput = ::GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION info{};
    const BOOL ok = ::CreateProcessW(nullptr, line->data(), nullptr, nullptr, TRUE,
                                     CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW |
                                         NORMAL_PRIORITY_CLASS,
                                     nullptr, nullptr, &startup, &info);
    if (ok == FALSE)
        return std::unexpected(LaunchError::SpawnFailed);

    const Handle spawnedThread(info.hThread);
    // dropping our copy of the write end is what lets the read end reach end of file
    writePipe.reset();

    processed_.store(0, std::memory_order_relaxed);
    total_.store(0, std::memory_order_relaxed);
    exitCode_.store(0, std::memory_order_relaxed);
    currentFile_.store({}, std::memory_order_release);
    finished_.store(false, std::memory_order_relaxed);
    running_.store(true, std::memory_order_release);

    join();
    thread_ = std::thread(&DefragJob::pump, this, readPipe.release(), info.hProcess);
    return {};
}

void DefragJob::pump(void* pipe, void* process)
{
    const Handle readEnd(static_cast<HANDLE>(pipe));
    const Handle child(static_cast<HANDLE>(process));

    std::string pending;
    std::array<char, 4096> buffer{};
    for (;;)
    {
        DWORD got = 0;
        if (::ReadFile(readEnd.get(), buffer.data(), static_cast<DWORD>(buffer.size()), &got,
                       nullptr) == FALSE ||
            got == 0)
            break;
        pending.append(buffer.data(), got);

        std::size_t start = 0;
        for (std::size_t brk = pending.find('\n', start); brk != std::string::npos;
             brk = pending.find('\n', start))
        {
            const std::string_view lineText(pending.data() + start, brk - start);
            start = brk + 1;

            if (const std::size_t at = lineText.find(progressMark); at != std::string_view::npos)
            {
                const std::string_view rest = lineText.substr(at + progressMark.size());
                if (const std::size_t slash = rest.find('/'); slash != std::string_view::npos)
                {
                    processed_.store(parseNumber(rest.substr(0, slash)),
                                     std::memory_order_relaxed);
                    total_.store(parseNumber(rest.substr(slash + 1)), std::memory_order_relaxed);
                }
            }
            else if (const std::size_t on = lineText.find(fileMark); on != std::string_view::npos)
            {
                std::string name(lineText.substr(on + fileMark.size()));
                while (!name.empty() && (name.back() == '\r' || name.back() == ' '))
                    name.pop_back();
                currentFile_.store(std::make_shared<const std::string>(std::move(name)),
                                   std::memory_order_release);
            }
        }
        pending.erase(0, start);
    }

    ::WaitForSingleObject(child.get(), INFINITE);
    DWORD code = 0;
    ::GetExitCodeProcess(child.get(), &code);
    exitCode_.store(static_cast<std::uint32_t>(code), std::memory_order_relaxed);
    core::info("defragmenter exited with {}", code);
    finished_.store(true, std::memory_order_release);
    running_.store(false, std::memory_order_release);
}

}
