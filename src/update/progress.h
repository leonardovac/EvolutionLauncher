#pragma once

#include <cstdint>
#include <string_view>

namespace wf
{

// Observer for a run's user-visible events; the CLI logs them, the GUI draws them.
class Progress
{
public:
	virtual ~Progress() = default;

	virtual void onIndex(std::size_t entries, std::size_t rejected) { (void)entries; (void)rejected; }
	virtual void onChecking(std::size_t checked, std::size_t total) { (void)checked; (void)total; }
	virtual void onPlan(std::size_t queued, std::uint64_t downloadBytes)
	{
		(void)queued;
		(void)downloadBytes;
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
};

}
