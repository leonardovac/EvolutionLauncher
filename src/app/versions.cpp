#include "app/versions.h"

#include "app/launch.h"

#include <windows.h>

#include <format>
#include <vector>

namespace app
{

std::optional<std::wstring> fileVersion(const std::filesystem::path& file)
{
    if (file.empty())
        return std::nullopt;
    DWORD ignored = 0;
    const DWORD size = ::GetFileVersionInfoSizeW(file.c_str(), &ignored);
    if (size == 0)
        return std::nullopt;
    std::vector<std::byte> buffer(size);
    if (::GetFileVersionInfoW(file.c_str(), 0, size, buffer.data()) == FALSE)
        return std::nullopt;
    VS_FIXEDFILEINFO* info = nullptr;
    UINT length = 0;
    if (::VerQueryValueW(buffer.data(), L"\\", reinterpret_cast<LPVOID*>(&info), &length) == FALSE)
        return std::nullopt;
    if (info == nullptr || length < sizeof(VS_FIXEDFILEINFO))
        return std::nullopt;
    return std::format(L"{}.{}.{}.{}", HIWORD(info->dwFileVersionMS), LOWORD(info->dwFileVersionMS),
                       HIWORD(info->dwFileVersionLS), LOWORD(info->dwFileVersionLS));
}

std::wstring launcherVersion()
{
    wchar_t path[MAX_PATH]{};
    const DWORD length = ::GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (length == 0 || length == MAX_PATH)
        return L"unknown";
    const auto version = fileVersion(std::filesystem::path(path, path + length));
    return version.value_or(L"unknown");
}

std::optional<std::wstring> engineVersion(const Settings& settings, wf::Branch branch)
{
    const std::filesystem::path root = settings.installRoot(branch);
    if (root.empty())
        return std::nullopt;
    return fileVersion(root / gameExeName(branch));
}

}
