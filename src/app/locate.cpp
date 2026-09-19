#include "app/locate.h"

#include "gfx/com.h"

#include <shobjidl.h>

namespace app
{

std::optional<std::filesystem::path> pickFolder(HWND owner)
{
    gfx::ComPtr<IFileOpenDialog> dialog;
    if (FAILED(::CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(dialog.put()))))
        return std::nullopt;
    DWORD options = 0;
    if (FAILED(dialog->GetOptions(&options)))
        return std::nullopt;
    // FORCEFILESYSTEM keeps the result a real path, not a virtual shell folder
    if (FAILED(dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM)))
        return std::nullopt;
    if (FAILED(dialog->Show(owner)))
        return std::nullopt;
    gfx::ComPtr<IShellItem> item;
    if (FAILED(dialog->GetResult(item.put())))
        return std::nullopt;
    wchar_t* name = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &name)) || name == nullptr)
        return std::nullopt;
    std::filesystem::path chosen(name);
    ::CoTaskMemFree(name);
    return chosen;
}

}
