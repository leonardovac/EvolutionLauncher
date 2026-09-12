#include "app/app.h"
#include "cli.h"

#include <windows.h>
#include <shellapi.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

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

bool argAfter(int argc, wchar_t** argv, std::wstring_view flag, std::wstring& value)
{
	for (int i = 1; i + 1 < argc; ++i)
	{
		if (flag == argv[i])
		{
			value = argv[i + 1];
			return true;
		}
	}
	return false;
}

bool argPresent(int argc, wchar_t** argv, std::wstring_view flag)
{
	for (int i = 1; i < argc; ++i)
	{
		if (flag == argv[i])
			return true;
	}
	return false;
}

}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
	int argc = 0;
	wchar_t** argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
	if (argv == nullptr)
		return 1;

	app::Options options;
	std::wstring value;
	options.wantShot = argc > 1 && std::wstring_view(argv[1]) == L"-shot";
	if (options.wantShot)
	{
		options.shotPath = L"shot.bmp";
		if (argAfter(argc, argv, L"-shot", value))
			options.shotPath = value;
		if (argAfter(argc, argv, L"-t", value))
			options.shotTime = static_cast<float>(::_wtof(value.c_str()));
		options.wantPanel = argPresent(argc, argv, L"-panel");
	}

	// Steam and Epic start the launcher with -registry:<tag>; that is not a CLI invocation
	bool guiOnly = true;
	for (int i = 1; i < argc; ++i)
	{
		if (!std::wstring_view(argv[i]).starts_with(L"-registry:"))
			guiOnly = false;
	}

	int result = 0;
	if (argc > 1 && !options.wantShot && !guiOnly)
	{
		attachConsole();
		result = wf::cli::run(argc, argv);
	}
	else
	{
		result = app::run(options);
	}

	::LocalFree(argv);
	return result;
}
