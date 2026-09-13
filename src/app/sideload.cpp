#include "app/sideload.h"

#include <windows.h>

#include <cstddef>
#include <fstream>

namespace app
{
namespace
{

template <typename T>
bool readAt(std::fstream& file, std::streamoff off, T& out)
{
    file.seekg(off);
    file.read(reinterpret_cast<char*>(&out), sizeof(T));
    return static_cast<bool>(file);
}

}

std::expected<bool, SideloadError> stripDependentLoadFlags(const std::filesystem::path& exe)
{
    std::fstream file(exe, std::ios::in | std::ios::out | std::ios::binary);
    if (!file)
        return std::unexpected(SideloadError::Open);

    IMAGE_DOS_HEADER dos{};
    if (!readAt(file, 0, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE)
        return std::unexpected(SideloadError::NotPe);

    IMAGE_NT_HEADERS64 nt{};
    if (!readAt(file, dos.e_lfanew, nt) || nt.Signature != IMAGE_NT_SIGNATURE)
        return std::unexpected(SideloadError::NotPe);
    if (nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
        return std::unexpected(SideloadError::Not64Bit);

    const IMAGE_DATA_DIRECTORY lc =
        nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG];
    if (lc.VirtualAddress == 0)
        return std::unexpected(SideloadError::NoLoadConfig);

    const std::streamoff sectionTable = static_cast<std::streamoff>(dos.e_lfanew) +
        offsetof(IMAGE_NT_HEADERS64, OptionalHeader) + nt.FileHeader.SizeOfOptionalHeader;
    const DWORD targetRva =
        lc.VirtualAddress + offsetof(IMAGE_LOAD_CONFIG_DIRECTORY64, DependentLoadFlags);

    std::streamoff fileOffset = 0;
    bool resolved = false;
    for (WORD i = 0; i < nt.FileHeader.NumberOfSections; ++i)
    {
        IMAGE_SECTION_HEADER sec{};
        if (!readAt(file,
                    sectionTable + static_cast<std::streamoff>(i) * sizeof(IMAGE_SECTION_HEADER),
                    sec))
            return std::unexpected(SideloadError::Resolve);
        if (targetRva >= sec.VirtualAddress &&
            targetRva < sec.VirtualAddress + sec.Misc.VirtualSize)
        {
            fileOffset =
                static_cast<std::streamoff>(sec.PointerToRawData) + (targetRva - sec.VirtualAddress);
            resolved = true;
            break;
        }
    }
    if (!resolved)
        return std::unexpected(SideloadError::Resolve);

    WORD flags = 0;
    if (!readAt(file, fileOffset, flags))
        return std::unexpected(SideloadError::Resolve);
    if (flags == 0)
        return false;

    file.clear();
    file.seekp(fileOffset);
    const WORD zero = 0;
    file.write(reinterpret_cast<const char*>(&zero), sizeof(zero));
    if (!file)
        return std::unexpected(SideloadError::Write);
    return true;
}

std::wstring_view describe(SideloadError error)
{
    switch (error)
    {
    case SideloadError::Open: return L"could not open the executable for patching";
    case SideloadError::NotPe: return L"not a PE executable";
    case SideloadError::Not64Bit: return L"not a 64-bit executable";
    case SideloadError::NoLoadConfig: return L"no load-config directory";
    case SideloadError::Resolve: return L"could not resolve the load-config address";
    case SideloadError::Write: return L"could not write the patched executable";
    }
    return L"unknown error";
}

}
