#include "app/applet.h"

#include "app/titles.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <utility>

namespace app
{
namespace
{

constexpr std::wstring_view consoleClass = L"ConsoleWindowClass";
// the name is unqualified, so it lives in the session namespace, same as the stock launcher's
constexpr const wchar_t* stopSemaphore = L"SoakStop";
constexpr int stopAttempts = 30;
constexpr DWORD stopWaitMs = 1000;

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

std::uint64_t fileSize(const std::wstring& path)
{
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (::GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data) == FALSE)
        return 0;
    return (static_cast<std::uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
}

}

std::expected<Handle, LaunchError> spawnHidden(std::wstring& line)
{
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    // AllocConsole gives the new console this show state, so it never appears
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION info{};
    const BOOL ok = ::CreateProcessW(nullptr, line.data(), nullptr, nullptr, FALSE,
                                     CREATE_UNICODE_ENVIRONMENT | NORMAL_PRIORITY_CLASS, nullptr,
                                     nullptr, &startup, &info);
    if (ok == FALSE)
        return std::unexpected(LaunchError::SpawnFailed);

    const Handle thread(info.hThread);
    return Handle(info.hProcess);
}

void hideAppletConsole(DWORD pid, HWND& console)
{
    if (console != nullptr)
        return;
    ConsoleSearch search{pid, nullptr};
    ::EnumWindows(findConsole, reinterpret_cast<LPARAM>(&search));
    console = search.found;
    // a hidden show state is not guaranteed, so close the gap if one slipped through
    if (console != nullptr && ::IsWindowVisible(console) != FALSE)
        ::ShowWindow(console, SW_HIDE);
}

void stopApplet(HANDLE process)
{
    for (int attempt = 0; attempt < stopAttempts; ++attempt)
    {
        if (const Handle signal(::CreateSemaphoreW(nullptr, 0, 1024, stopSemaphore)); signal)
            ::ReleaseSemaphore(signal.get(), 1, nullptr);
        if (::WaitForSingleObject(process, stopWaitMs) != WAIT_TIMEOUT)
            return;
    }
    ::TerminateProcess(process, 0);
    ::WaitForSingleObject(process, INFINITE);
}

std::wstring gameLogPath(wf::Title title, std::wstring_view name)
{
    std::array<wchar_t, MAX_PATH> local{};
    const DWORD written = ::GetEnvironmentVariableW(L"LOCALAPPDATA", local.data(), static_cast<DWORD>(local.size()));
    if (written == 0 || written >= local.size())
        return {};
    return (std::filesystem::path(local.data()) / profile(title).localFolder / name).wstring();
}

LogTail::LogTail(std::wstring path) : path_(std::move(path)), offset_(path_.empty() ? 0 : fileSize(path_))
{
}

bool LogTail::fill()
{
    const core::File file(::CreateFileW(path_.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file)
        return false;
    LARGE_INTEGER size{};
    if (::GetFileSizeEx(file.get(), &size) == FALSE)
        return false;
    const auto end = static_cast<std::uint64_t>(size.QuadPart);
    if (end < offset_)
    {
        offset_ = 0;
        pending_.clear();
    }
    LARGE_INTEGER seek{};
    seek.QuadPart = static_cast<LONGLONG>(offset_);
    if (::SetFilePointerEx(file.get(), seek, nullptr, FILE_BEGIN) == FALSE)
        return false;

    std::array<char, 16 * 1024> chunk{};
    while (offset_ < end)
    {
        const auto want = static_cast<DWORD>(std::min<std::uint64_t>(chunk.size(), end - offset_));
        DWORD got = 0;
        if (::ReadFile(file.get(), chunk.data(), want, &got, nullptr) == FALSE || got == 0)
            break;
        offset_ += got;
        pending_.append(chunk.data(), got);
    }
    return true;
}

}
