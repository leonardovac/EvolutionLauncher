#include "app/versions.h"

#include "app/launch.h"

#include <windows.h>

#include <format>
#include <vector>

namespace app
{

namespace
{

std::optional<std::vector<std::byte>> versionInfo(const std::filesystem::path& file)
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
    return buffer;
}

std::optional<std::wstring> fileVersionText(const std::filesystem::path& file)
{
    const auto buffer = versionInfo(file);
    if (!buffer)
        return std::nullopt;
    wchar_t* text = nullptr;
    UINT length = 0;
    // our own resource declares exactly one translation, 0x409/1200
    if (::VerQueryValueW(buffer->data(), LR"(\StringFileInfo\040904B0\FileVersion)",
                         reinterpret_cast<LPVOID*>(&text), &length) == FALSE)
        return std::nullopt;
    if (text == nullptr || length == 0)
        return std::nullopt;
    const std::wstring_view value(text, ::wcsnlen(text, length));
    if (value.empty())
        return std::nullopt;
    return std::wstring(value);
}

}

std::optional<std::wstring> fileVersion(const std::filesystem::path& file)
{
    const auto buffer = versionInfo(file);
    if (!buffer)
        return std::nullopt;
    VS_FIXEDFILEINFO* info = nullptr;
    UINT length = 0;
    if (::VerQueryValueW(buffer->data(), L"\\", reinterpret_cast<LPVOID*>(&info), &length) == FALSE)
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
    const std::filesystem::path self(path, path + length);
    // the string carries the prerelease suffix the fixed-info fields cannot hold
    if (const auto text = fileVersionText(self))
        return *text;
    return fileVersion(self).value_or(L"unknown");
}

std::optional<std::wstring> gameBuildVersion(const Settings& settings, wf::Branch branch)
{
    const std::filesystem::path root = settings.installRoot(branch);
    if (root.empty())
        return std::nullopt;
    return fileVersion(root / gameExeName(settings.title));
}

}
