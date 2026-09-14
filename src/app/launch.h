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
    NoCachePlan,
    NoSpace,
    SpawnFailed
};

// -registry:<tag> as this process was started with it; the game takes it as -clienttype
std::optional<std::wstring> registryTag();

std::wstring_view gameExeName(wf::Branch branch);

std::wstring buildGameCommandLine(const Settings& settings, wf::Branch branch,
                                  const std::filesystem::path& root);

// false when the branch root holds no game executable, so there is nothing to launch yet
bool gameInstalled(const Settings& settings, wf::Branch branch);

std::expected<void, LaunchError> launchGame(const Settings& settings, wf::Branch branch);

// runs the game's own cache defragmenter; removes a stale Defrag.log first
std::expected<void, LaunchError> launchDefrag(const Settings& settings, wf::Branch branch);

// the defragmenter line and its guards, without spawning; also clears a stale Defrag.log
std::expected<std::wstring, LaunchError> buildDefragCommandLine(const Settings& settings,
                                                                wf::Branch branch);

std::wstring_view describe(LaunchError error);

}
