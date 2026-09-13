#pragma once

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

std::wstring_view describe(SideloadError error);

}
