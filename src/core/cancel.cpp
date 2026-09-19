#include "core/cancel.h"

#include "core/log.h"

#include <windows.h>

namespace core
{
namespace
{

CancelToken* installed = nullptr;

BOOL WINAPI onConsoleEvent(DWORD type)
{
	constexpr DWORD handled[] = {CTRL_C_EVENT, CTRL_BREAK_EVENT, CTRL_CLOSE_EVENT};
	for (const DWORD event : handled)
	{
		if (event != type || installed == nullptr)
			continue;
		// returning TRUE keeps the process alive so the current entry can unwind
		if (!installed->requested())
		{
			installed->request();
			write(Level::Warn, "cancelling, finishing the current file");
		}
		return TRUE;
	}
	return FALSE;
}

}

void installCancelHandler(CancelToken& token)
{
	installed = &token;
	::SetConsoleCtrlHandler(onConsoleEvent, TRUE);
}

}
