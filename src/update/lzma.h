#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <span>
#include <string_view>

namespace wf
{

enum class LzmaError
{
	Header,
	Allocate,
	Corrupt,
	Sink
};

using ByteSink = std::function<bool(std::span<const std::uint8_t>)>;

// LZMA-alone container: 5 props bytes, 8-byte LE uncompressed size, then the raw stream.
class LzmaDecoder
{
public:
	LzmaDecoder();
	~LzmaDecoder();
	LzmaDecoder(const LzmaDecoder&) = delete;
	LzmaDecoder& operator=(const LzmaDecoder&) = delete;

	std::expected<void, LzmaError> push(std::span<const std::uint8_t> input, const ByteSink& sink);

	[[nodiscard]] bool complete() const noexcept;
	[[nodiscard]] std::uint64_t expectedSize() const noexcept;

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

std::wstring_view describe(LzmaError error);

}
