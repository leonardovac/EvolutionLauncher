#include "app/assets.h"

#include <windows.h>

namespace app
{

std::span<const std::uint8_t> resource(int id)
{
    HMODULE module = ::GetModuleHandleW(nullptr);
    HRSRC found = ::FindResourceW(module, MAKEINTRESOURCEW(id), RT_RCDATA);
    if (found == nullptr)
        return {};
    const DWORD size = ::SizeofResource(module, found);
    HGLOBAL loaded = ::LoadResource(module, found);
    if (loaded == nullptr || size == 0)
        return {};
    const auto* bytes = static_cast<const std::uint8_t*>(::LockResource(loaded));
    return bytes != nullptr ? std::span<const std::uint8_t>(bytes, size) : std::span<const std::uint8_t>{};
}

}
