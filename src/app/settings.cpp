#include "app/settings.h"

#include "app/launch.h"
#include "app/titles.h"
#include "update/config.h"

#include "core/str.h"

#include <windows.h>

#include <array>
#include <optional>
#include <string>

namespace app
{
namespace
{

std::wstring keyFor(wf::Title title)
{
    return std::wstring(profile(title).registrySubkey);
}

std::optional<std::wstring> readString(const std::wstring& key, const wchar_t* name)
{
    std::array<wchar_t, 1024> buffer{};
    DWORD size = static_cast<DWORD>(buffer.size() * sizeof(wchar_t));
    if (::RegGetValueW(HKEY_CURRENT_USER, key.c_str(), name, RRF_RT_REG_SZ, nullptr, buffer.data(),
                       &size) != ERROR_SUCCESS)
        return std::nullopt;
    return std::wstring(buffer.data());
}

std::optional<DWORD> readDword(const std::wstring& key, const wchar_t* name)
{
    DWORD value = 0;
    DWORD size = sizeof(value);
    if (::RegGetValueW(HKEY_CURRENT_USER, key.c_str(), name, RRF_RT_REG_DWORD, nullptr, &value,
                       &size) != ERROR_SUCCESS)
        return std::nullopt;
    return value;
}

bool writeString(HKEY key, const wchar_t* name, std::wstring_view value)
{
    const DWORD bytes = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
    const std::wstring owned(value);
    return ::RegSetValueExW(key, name, 0, REG_SZ,
                            reinterpret_cast<const BYTE*>(owned.c_str()), bytes) == ERROR_SUCCESS;
}

bool writeDword(HKEY key, const wchar_t* name, DWORD value)
{
    return ::RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value),
                            sizeof(value)) == ERROR_SUCCESS;
}

template <typename E>
E clampEnum(DWORD raw, E fallback, DWORD highest)
{
    return raw <= highest ? static_cast<E>(raw) : fallback;
}

}

bool writeDwordTo(std::wstring_view path, const wchar_t* name, DWORD value)
{
    HKEY key = nullptr;
    if (::RegCreateKeyExW(HKEY_CURRENT_USER, std::wstring(path).c_str(), 0, nullptr,
                          REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &key, nullptr) !=
        ERROR_SUCCESS)
        return false;
    const bool ok = writeDword(key, name, value);
    ::RegCloseKey(key);
    return ok;
}

Settings Settings::load(wf::Title title, const wf::LauncherConfig& launcher)
{
    Settings out;
    out.title = title;
    const std::wstring key = keyFor(title);
    if (const auto value = readDword(key, L"GraphicsAPI"))
        out.graphicsApi = clampEnum(*value, GraphicsApi::Dx11, 1u);
    if (const auto value = readDword(key, L"GPUPreference"))
        out.gpuPreference = clampEnum(*value, GpuPreference::LetWindowsDecide, 2u);
    if (const auto value = readDword(key, L"WindowMode"))
        out.windowMode = clampEnum(*value, WindowMode::Windowed, 2u);
    if (const auto value = readString(key, L"Language"); value && value->size() == 2)
        out.language = *value;
    if (const auto value = readString(key, L"LanguageVO"); value && value->size() == 2)
        out.audioLanguage = *value;
    if (const auto value = readDword(key, L"EnableShaderCache"))
        out.shaderCache = *value != 0;
    // launcher-wide, so it lives in launcher.json; an older install is imported from ForceHTTPS
    if (const auto stored = launcher.allowNetworkCaches())
        out.allowNetworkCaches = *stored;
    else if (const auto legacy = readDword(key, L"ForceHTTPS"))
        out.allowNetworkCaches = *legacy == 0;
    if (const auto stored = launcher.sideload())
        out.sideload = *stored;
    if (const auto value = readString(key, L"LauncherExe"))
        out.launcherExe = *value;
    return out;
}

bool Settings::save(const Settings* baseline) const
{
    HKEY key = nullptr;
    if (::RegCreateKeyExW(HKEY_CURRENT_USER, keyFor(title).c_str(), 0, nullptr,
                          REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &key,
                          nullptr) != ERROR_SUCCESS)
        return false;

    bool ok = true;
    if (!baseline || graphicsApi != baseline->graphicsApi)
        ok = writeDword(key, L"GraphicsAPI", static_cast<DWORD>(graphicsApi)) && ok;
    if (!baseline || gpuPreference != baseline->gpuPreference)
        ok = writeDword(key, L"GPUPreference", static_cast<DWORD>(gpuPreference)) && ok;
    if (!baseline || windowMode != baseline->windowMode)
        ok = writeDword(key, L"WindowMode", static_cast<DWORD>(windowMode)) && ok;
    if (!baseline || language != baseline->language)
        ok = writeString(key, L"Language", language) && ok;
    if (!baseline || audioLanguage != baseline->audioLanguage)
        ok = writeString(key, L"LanguageVO", audioLanguage) && ok;
    if (!baseline || shaderCache != baseline->shaderCache)
        ok = writeDword(key, L"EnableShaderCache", shaderCache ? 1u : 0u) && ok;

    ::RegCloseKey(key);

    // ours to act on, so it is stored launcher-wide; each title's stock launcher still reads
    // its own ForceHTTPS, so the choice is mirrored into every one of them
    if (!baseline || allowNetworkCaches != baseline->allowNetworkCaches
        || sideload != baseline->sideload)
    {
        wf::LauncherConfig launcher = wf::LauncherConfig::load();
        launcher.setAllowNetworkCaches(allowNetworkCaches);
        launcher.setSideload(sideload);
        ok = launcher.save() && ok;
        for (const TitleProfile& entry : titleProfiles())
            ok = writeDwordTo(entry.registrySubkey, L"ForceHTTPS", allowNetworkCaches ? 0u : 1u)
                && ok;
    }
    return ok;
}

bool Settings::adoptInstallRoot(const std::filesystem::path& folder)
{
    std::error_code ec;
    if (folder.empty() || !std::filesystem::exists(folder / gameExeName(title), ec))
        return false;
    // the key names the stock launcher, and installRoot() reads the root back off its path
    const std::filesystem::path exe = folder / L"Tools" / L"Launcher.exe";
    HKEY key = nullptr;
    if (::RegCreateKeyExW(HKEY_CURRENT_USER, keyFor(title).c_str(), 0, nullptr,
                          REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &key,
                          nullptr) != ERROR_SUCCESS)
        return false;
    const bool ok = writeString(key, L"LauncherExe", exe.wstring());
    ::RegCloseKey(key);
    if (ok)
        launcherExe = exe;
    return ok;
}

std::filesystem::path Settings::installRoot(wf::Branch branch) const
{
    if (branch == wf::Branch::Public && !launcherExe.empty())
    {
        const std::filesystem::path root = launcherExe.parent_path().parent_path();
        std::error_code ec;
        if (!root.empty() && std::filesystem::exists(root, ec))
            return root;
    }

    std::array<wchar_t, MAX_PATH> local{};
    const DWORD written =
        ::GetEnvironmentVariableW(L"LOCALAPPDATA", local.data(), static_cast<DWORD>(local.size()));
    if (written == 0 || written >= local.size())
        return {};
    return std::filesystem::path(local.data()) / profile(title).localFolder / L"Downloaded"
           / wf::branchName(branch);
}

bool Settings::steam() const
{
    return core::containsNoCase(launcherExe.wstring(), L"steamapps");
}

bool Settings::eos() const
{
    return core::containsNoCase(launcherExe.wstring(), L"Epic");
}

}
