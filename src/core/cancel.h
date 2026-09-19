#pragma once

#include <atomic>

namespace core
{

// One run's cancellation flag. Owned by the caller, so two runs never share one.
class CancelToken
{
public:
	void request() noexcept { flag_.store(true, std::memory_order_relaxed); }
	void reset() noexcept { flag_.store(false, std::memory_order_relaxed); }
	[[nodiscard]] bool requested() const noexcept { return flag_.load(std::memory_order_relaxed); }

private:
	std::atomic<bool> flag_{false};
};

// the console handler keeps a pointer to this token; it must outlive the handler
void installCancelHandler(CancelToken& token);

}
