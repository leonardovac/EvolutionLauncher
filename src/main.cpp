#include "cli.h"

#include <windows.h>
#include <shellapi.h>

#include <cstdio>

namespace
{

void attachConsole()
{
	if (!::AttachConsole(ATTACH_PARENT_PROCESS) && !::AllocConsole())
		return;
	FILE* stream = nullptr;
	::freopen_s(&stream, "CONOUT$", "w", stdout);
	::freopen_s(&stream, "CONOUT$", "w", stderr);
}

}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
	int argc = 0;
	wchar_t** argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
	if (argv == nullptr)
		return 1;

	int result = 0;
	if (argc > 1)
	{
		attachConsole();
		result = wf::cli::run(argc, argv);
	}
	else
	{
		std::fputs("GUI not implemented yet\n", stderr);
		result = 1;
	}

	::LocalFree(argv);
	return result;
}
