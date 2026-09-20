#pragma once

#include "update/config.h"
#include "update/manifest.h"

#include <expected>
#include <filesystem>
#include <string_view>

namespace app
{

enum class SideloadError
{
    Open,
    NotPe,
    Not64Bit,
    NoLoadConfig,
    Resolve,
    Write
};

// zeroes the PE DependentLoadFlags so the loader searches the app dir for DLLs
std::expected<bool, SideloadError> stripDependentLoadFlags(const std::filesystem::path& exe);

// true while the exe still carries a non-zero DependentLoadFlags, so a patch is pending
std::expected<bool, SideloadError> dependentLoadFlagsSet(const std::filesystem::path& exe);

// patches the installed game exe once per index version and remembers the result in launcher.json
void ensureSideloaded(wf::LauncherConfig& config, const wf::Entry& mainExe,
                      const std::filesystem::path& root);

// patches at launch, where no index is at hand, so the toggle holds without a check first
void ensureSideloadedAtLaunch(const std::filesystem::path& exe, std::wstring_view installPath);

std::wstring_view describe(SideloadError error);

}
