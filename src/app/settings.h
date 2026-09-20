#pragma once

#include "update/plan.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace app
{

enum class GraphicsApi : std::uint32_t
{
    Dx11 = 0,
    Dx12 = 1
};

enum class GpuPreference : std::uint32_t
{
    LetWindowsDecide = 0,
    PowerSaving = 1,
    HighPerformance = 2
};

enum class WindowMode : std::uint32_t
{
    Windowed = 0,
    Fullscreen = 1,
    Borderless = 2
};

enum class RootProbe
{
    HasGame,
    // no entries at all, so a full install may go here
    Empty,
    // files, but not this title's: adopting it would put the purge over someone else's folder
    Occupied,
    Unusable
};

// what a folder is to this title, without changing anything
RootProbe probeInstallRoot(wf::Title title, const std::filesystem::path& folder);

struct Settings
{
    wf::Title title = wf::Title::Warframe;
    GraphicsApi graphicsApi = GraphicsApi::Dx11;
    GpuPreference gpuPreference = GpuPreference::LetWindowsDecide;
    WindowMode windowMode = WindowMode::Windowed;
    std::wstring language = L"en";
    std::wstring audioLanguage;
    bool shaderCache = true;
    // the stock launcher owns this one: read, never written back
    bool bulkDownload = true;
    // launcher-wide: stored in launcher.json, mirrored into each title's ForceHTTPS
    bool allowNetworkCaches = true;
    // per title: patch this game's exe so the loader searches its folder for DLLs
    bool sideload = true;
    // DE's, read-only for us: the stock launcher's path, which still names the platform
    std::filesystem::path launcherExe;
    // DE's, read-only for us: the parent of the branch directories
    std::filesystem::path downloadDir;
    // ours, from launcher.json; Public only, the same limit launcherExe carries
    std::filesystem::path installRootOverride;

    // the caller owns the one launcher.json read; loading it here would parse the file twice
    static Settings load(wf::Title title, const wf::LauncherConfig& launcher);
    bool save(const Settings* baseline = nullptr) const;

    // records the folder when it holds the game or is empty; save() is what writes it out
    RootProbe adoptInstallRoot(const std::filesystem::path& folder);

    [[nodiscard]] std::filesystem::path installRoot(wf::Branch branch) const;
    [[nodiscard]] bool steam() const;
    [[nodiscard]] bool eos() const;
    [[nodiscard]] bool dx12() const noexcept { return graphicsApi == GraphicsApi::Dx12; }
};

}
