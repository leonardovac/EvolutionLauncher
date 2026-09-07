#pragma once

#include <windows.h>

#include <utility>

namespace core
{

template <class Traits>
class UniqueHandle
{
public:
	using Value = typename Traits::Value;

	UniqueHandle() = default;
	explicit UniqueHandle(Value value) noexcept : value_(value) {}
	UniqueHandle(const UniqueHandle&) = delete;
	UniqueHandle& operator=(const UniqueHandle&) = delete;

	UniqueHandle(UniqueHandle&& other) noexcept
		: value_(std::exchange(other.value_, Traits::invalid()))
	{
	}

	UniqueHandle& operator=(UniqueHandle&& other) noexcept
	{
		if (this != &other)
			reset(std::exchange(other.value_, Traits::invalid()));
		return *this;
	}

	~UniqueHandle() { reset(); }

	[[nodiscard]] Value get() const noexcept { return value_; }
	[[nodiscard]] explicit operator bool() const noexcept { return value_ != Traits::invalid(); }

	void reset(Value value = Traits::invalid()) noexcept
	{
		if (value_ != Traits::invalid())
			Traits::close(value_);
		value_ = value;
	}

	[[nodiscard]] Value release() noexcept { return std::exchange(value_, Traits::invalid()); }

private:
	Value value_ = Traits::invalid();
};

struct FileTraits
{
	using Value = HANDLE;
	static Value invalid() noexcept { return INVALID_HANDLE_VALUE; }
	static void close(Value value) noexcept { ::CloseHandle(value); }
};

using File = UniqueHandle<FileTraits>;

}
