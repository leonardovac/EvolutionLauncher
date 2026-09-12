#pragma once

#include "app/settings.h"
#include "update/plan.h"

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace app
{

enum class LaunchError
{
    NoRoot,
    NoExecutable,
    SpawnFailed
};

// -registry:<tag> as this process was started with it; the game takes it as -clienttype
std::optional<std::wstring> registryTag();

std::wstring buildGameCommandLine(const Settings& settings, wf::Branch branch,
                                  const std::filesystem::path& root);

std::expected<void, LaunchError> launchGame(const Settings& settings, wf::Branch branch);

std::wstring_view describe(LaunchError error);

}
