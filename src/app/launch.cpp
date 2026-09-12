#include "app/launch.h"

#include "core/log.h"
#include "core/str.h"
#include "core/win.h"

#include <windows.h>
#include <shellapi.h>

#include <array>
#include <filesystem>
#include <format>

namespace app
{
namespace
{

constexpr std::wstring_view exeName = L"Warframe.x64.exe";

constexpr std::array<std::wstring_view, 3> clusterArgs{L" -cluster:public", L" -cluster:test",
                                                       L" -cluster:dev"};
constexpr std::array<std::wstring_view, 2> graphicsDriverNames{L"dx11", L"dx12"};

std::wstring_view clusterArg(wf::Branch branch)
{
    const auto index = static_cast<std::size_t>(branch);
    return index < clusterArgs.size() ? clusterArgs[index] : clusterArgs[0];
}

std::wstring_view graphicsDriverName(GraphicsApi api)
{
    const auto index = static_cast<std::size_t>(api);
    return index < graphicsDriverNames.size() ? graphicsDriverNames[index]
                                              : graphicsDriverNames[0];
}

struct ProcessTraits
{
    using Value = HANDLE;
    static Value invalid() noexcept { return nullptr; }
    static void close(Value value) noexcept { ::CloseHandle(value); }
};

using ProcessHandle = core::UniqueHandle<ProcessTraits>;

}

std::optional<std::wstring> registryTag()
{
    int count = 0;
    wchar_t** argv = ::CommandLineToArgvW(::GetCommandLineW(), &count);
    if (argv == nullptr)
        return std::nullopt;
    constexpr std::wstring_view prefix = L"-registry:";
    std::optional<std::wstring> tag;
    for (int i = 1; i < count; ++i)
    {
        const std::wstring_view arg(argv[i]);
        if (arg.starts_with(prefix) && arg.size() > prefix.size())
            tag = std::wstring(arg.substr(prefix.size()));
    }
    ::LocalFree(argv);
    return tag;
}

std::wstring buildGameCommandLine(const Settings& settings, wf::Branch branch,
                                  const std::filesystem::path& root)
{
    if (root.empty())
        return {};

    const std::filesystem::path exe = root / exeName;

    std::wstring line = std::format(
        L"\"{}\" -windowMode:{} -shaderCache:{} -graphicsDriver:{} -gpuPreference:{}",
        exe.wstring(), static_cast<std::uint32_t>(settings.windowMode),
        settings.shaderCache ? 1u : 0u, graphicsDriverName(settings.graphicsApi),
        static_cast<std::uint32_t>(settings.gpuPreference));
    line += clusterArg(branch);
    line += std::format(L" -language:{}", settings.language);
    if (!settings.audioLanguage.empty())
        line += std::format(L" -languageVO:{}", settings.audioLanguage);
    if (const auto tag = registryTag())
        line += std::format(L" -clienttype:{}", *tag);
    if (!settings.allowNetworkCaches)
        line += L" -forceHTTPS";
    return line;
}

std::expected<void, LaunchError> launchGame(const Settings& settings, wf::Branch branch)
{
    const std::filesystem::path root = settings.installRoot(branch);
    std::wstring line = buildGameCommandLine(settings, branch, root);
    if (line.empty())
        return std::unexpected(LaunchError::NoRoot);

    const std::filesystem::path exe = root / exeName;
    std::error_code ec;
    if (!std::filesystem::exists(exe, ec))
        return std::unexpected(LaunchError::NoExecutable);

    core::info("launching: {}", core::narrow(line));

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info{};
    const BOOL ok = ::CreateProcessW(nullptr, line.data(), nullptr, nullptr, FALSE,
                                     CREATE_UNICODE_ENVIRONMENT | NORMAL_PRIORITY_CLASS, nullptr,
                                     nullptr, &startup, &info);
    if (ok == FALSE)
        return std::unexpected(LaunchError::SpawnFailed);

    const ProcessHandle process(info.hProcess);
    const ProcessHandle thread(info.hThread);
    return {};
}

std::wstring_view describe(LaunchError error)
{
    switch (error)
    {
    case LaunchError::NoRoot:
        return L"could not resolve the install root";
    case LaunchError::NoExecutable:
        return L"the game executable is missing";
    case LaunchError::SpawnFailed:
        return L"could not start the game";
    }
    return L"unknown launch error";
}

}
