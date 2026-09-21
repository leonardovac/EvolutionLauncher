#pragma once

#include <windows.h>

#include <filesystem>
#include <optional>

namespace app
{

// the folder the user chose, nothing when the dialog was dismissed; COM must already be live
std::optional<std::filesystem::path> pickFolder(HWND owner, const std::filesystem::path& startAt = {});

}
