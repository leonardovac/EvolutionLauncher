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

constexpr std::wstring_view defragArgs =
    L" -applet:/EE/Types/Framework/CacheDefraggerIOCP /Tools/CachePlan.txt";
constexpr std::wstring_view defragLog = L"Defrag.log";

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

std::expected<void, LaunchError> spawn(std::wstring& line)
{
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

}

std::wstring_view gameExeName(wf::Branch branch)
{
    switch (branch)
    {
    case wf::Branch::Public:
    case wf::Branch::Test:
    case wf::Branch::Dev:
        break;
    }
    return L"Warframe.x64.exe";
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

    const std::filesystem::path exe = root / gameExeName(branch);

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

    const std::filesystem::path exe = root / gameExeName(branch);
    std::error_code ec;
    if (!std::filesystem::exists(exe, ec))
        return std::unexpected(LaunchError::NoExecutable);

    return spawn(line);
}

std::expected<void, LaunchError> launchDefrag(const Settings& settings, wf::Branch branch)
{
    auto line = buildDefragCommandLine(settings, branch);
    if (!line)
        return std::unexpected(line.error());
    return spawn(*line);
}

std::expected<std::wstring, LaunchError> buildDefragCommandLine(const Settings& settings,
                                                                wf::Branch branch)
{
    const std::filesystem::path root = settings.installRoot(branch);
    std::wstring line = buildGameCommandLine(settings, branch, root);
    if (line.empty())
        return std::unexpected(LaunchError::NoRoot);

    std::error_code ec;
    if (!std::filesystem::exists(root / gameExeName(branch), ec))
        return std::unexpected(LaunchError::NoExecutable);

    std::error_code planEc;
    const std::uintmax_t plan =
        std::filesystem::file_size(root / L"Tools" / L"CachePlan.txt", planEc);
    if (planEc)
        return std::unexpected(LaunchError::NoCachePlan);

    std::error_code spaceEc;
    const std::filesystem::space_info space = std::filesystem::space(root, spaceEc);
    if (spaceEc || space.available < plan + plan / 2)
        return std::unexpected(LaunchError::NoSpace);

    // the launcher's own artefact from a previous run, not user content
    ec.clear();
    std::filesystem::remove(root / defragLog, ec);
    if (ec)
        core::warn("could not remove {}: {}", core::narrow(defragLog), ec.message());

    line += defragArgs;
    return line;
}

std::wstring_view describe(LaunchError error)
{
    switch (error)
    {
    case LaunchError::NoRoot:
        return L"could not resolve the install root";
    case LaunchError::NoExecutable:
        return L"the game executable is missing";
    case LaunchError::NoCachePlan:
        return L"could not read Tools/CachePlan.txt to size the defragment guard";
    case LaunchError::NoSpace:
        return L"not enough free disk space to defragment safely";
    case LaunchError::SpawnFailed:
        return L"could not start the game";
    }
    return L"unknown launch error";
}

}
