#pragma once

#include "core/cancel.h"
#include "core/log.h"

#include <cstdint>
#include <string_view>

namespace wf
{

class Progress
{
public:
	virtual ~Progress() = default;

	virtual void onIndex(std::size_t entries, std::size_t rejected) { (void)entries; (void)rejected; }
	virtual void onChecking(std::size_t checked, std::size_t total, std::uint64_t hashedBytes)
	{
		(void)checked;
		(void)total;
		(void)hashedBytes;
	}
	virtual void onPlan(std::size_t queued, std::uint64_t downloadBytes, std::size_t filtered,
	                    std::size_t skipped, std::size_t upToDate, std::size_t bulkSkipped)
	{
		(void)queued;
		(void)downloadBytes;
		(void)filtered;
		(void)skipped;
		(void)upToDate;
		(void)bulkSkipped;
	}
	virtual void onEntryStart(std::size_t index, std::size_t count, std::wstring_view installPath,
	                          std::uint64_t wireSize)
	{
		(void)index;
		(void)count;
		(void)installPath;
		(void)wireSize;
	}
	virtual void onBytes(std::uint64_t bytes) { (void)bytes; }
	virtual void onEntryFailed(std::wstring_view installPath, std::wstring_view reason)
	{
		(void)installPath;
		(void)reason;
	}
	virtual void onStale(std::wstring_view installPath, std::uint64_t bytes)
	{
		(void)installPath;
		(void)bytes;
	}
	// paths stay UTF-16 to here; the sink narrows if its output needs it
	virtual void onLog(core::Level level, std::wstring_view message)
	{
		(void)level;
		(void)message;
	}
};

// drops everything, so a default-built context is safe to use rather than a null to check for
inline Progress& nullProgress()
{
	static Progress instance;
	return instance;
}

// Everything a run needs beyond its Config: where output goes and how it is stopped.
struct RunContext
{
	Progress* progress = &nullProgress();  // never null
	const core::CancelToken* cancel = nullptr;

	[[nodiscard]] bool cancelled() const noexcept
	{
		return cancel != nullptr && cancel->requested();
	}
	void log(core::Level level, std::wstring_view message) const
	{
		progress->onLog(level, message);
	}
};

}
