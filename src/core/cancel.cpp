#include "core/cancel.h"

#include "core/log.h"

#include <windows.h>

#include <atomic>

namespace core
{
namespace
{

std::atomic<bool> stopping{false};

BOOL WINAPI onConsoleEvent(DWORD type)
{
	constexpr DWORD handled[] = {CTRL_C_EVENT, CTRL_BREAK_EVENT, CTRL_CLOSE_EVENT};
	for (const DWORD event : handled)
	{
		if (event != type)
			continue;
		// returning TRUE keeps the process alive so the current entry can unwind
		if (!stopping.exchange(true))
			write(Level::Warn, "cancelling, finishing the current file");
		return TRUE;
	}
	return FALSE;
}

}

void installCancelHandler()
{
	::SetConsoleCtrlHandler(onConsoleEvent, TRUE);
}

bool cancelled() noexcept
{
	return stopping.load(std::memory_order_relaxed);
}

void requestCancel() noexcept
{
	stopping.store(true, std::memory_order_relaxed);
}

void resetCancel() noexcept
{
	stopping.store(false, std::memory_order_relaxed);
}

}
