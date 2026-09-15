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

struct Settings
{
    GraphicsApi graphicsApi = GraphicsApi::Dx11;
    GpuPreference gpuPreference = GpuPreference::LetWindowsDecide;
    WindowMode windowMode = WindowMode::Windowed;
    std::wstring language = L"en";
    std::wstring audioLanguage;
    bool shaderCache = true;
    // launcher-wide: stored in launcher.json, mirrored into each title's ForceHTTPS
    bool allowNetworkCaches = true;
    std::filesystem::path launcherExe;

    // the caller owns the one launcher.json read; loading it here would parse the file twice
    static Settings load(const wf::LauncherConfig& launcher);
    bool save(const Settings* baseline = nullptr) const;

    [[nodiscard]] std::filesystem::path installRoot(wf::Branch branch) const;
    [[nodiscard]] bool steam() const;
    [[nodiscard]] bool eos() const;
    [[nodiscard]] bool dx12() const noexcept { return graphicsApi == GraphicsApi::Dx12; }
};

}
