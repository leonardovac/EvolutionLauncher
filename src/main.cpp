#include "cli.h"

#include <windows.h>
#include <shellapi.h>

#include <cstdio>

namespace
{

// GUI builds have no console; borrow the caller's without clobbering a redirect.
void attachConsole()
{
	const bool outRedirected = ::GetFileType(::GetStdHandle(STD_OUTPUT_HANDLE)) != FILE_TYPE_UNKNOWN;
	const bool errRedirected = ::GetFileType(::GetStdHandle(STD_ERROR_HANDLE)) != FILE_TYPE_UNKNOWN;
	if (outRedirected && errRedirected)
		return;
	if (!::AttachConsole(ATTACH_PARENT_PROCESS) && !::AllocConsole())
		return;
	FILE* stream = nullptr;
	if (!outRedirected)
		::freopen_s(&stream, "CONOUT$", "w", stdout);
	if (!errRedirected)
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
