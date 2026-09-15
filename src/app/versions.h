#pragma once

#include "app/settings.h"
#include "update/plan.h"

#include <filesystem>
#include <optional>
#include <string>

namespace app
{

std::optional<std::wstring> fileVersion(const std::filesystem::path& file);

std::wstring launcherVersion();

std::optional<std::wstring> gameBuildVersion(const Settings& settings, wf::Branch branch);

}
