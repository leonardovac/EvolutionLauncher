#include "update/lzma.h"

#include <lzma/LzmaDec.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace wf
{
namespace
{

constexpr std::size_t headerSize = 13;
constexpr std::size_t outChunk = 32u * 1024u;
constexpr std::uint64_t unknownSize = ~0ull;

void* lzmaAlloc(ISzAllocPtr, std::size_t size)
{
	return std::malloc(size);
}

void lzmaFree(ISzAllocPtr, void* address)
{
	std::free(address);
}

const ISzAlloc allocator{lzmaAlloc, lzmaFree};

}

struct LzmaDecoder::Impl
{
	CLzmaDec state{};
	bool allocated = false;
	std::array<std::uint8_t, headerSize> header{};
	std::size_t headerFilled = 0;
	std::uint64_t expected = 0;
	std::uint64_t remaining = 0;
	std::vector<std::uint8_t> out = std::vector<std::uint8_t>(outChunk);
};

LzmaDecoder::LzmaDecoder() : impl_(std::make_unique<Impl>())
{
	LzmaDec_Construct(&impl_->state);
}

LzmaDecoder::~LzmaDecoder()
{
	if (impl_->allocated)
		LzmaDec_Free(&impl_->state, &allocator);
}

std::expected<void, LzmaError> LzmaDecoder::push(std::span<const std::uint8_t> input,
                                                 const ByteSink& sink)
{
	if (impl_->headerFilled < headerSize)
	{
		const std::size_t take = (std::min)(input.size(), headerSize - impl_->headerFilled);
		std::memcpy(impl_->header.data() + impl_->headerFilled, input.data(), take);
		impl_->headerFilled += take;
		input = input.subspan(take);
		if (impl_->headerFilled < headerSize)
			return {};

		if (LzmaDec_Allocate(&impl_->state, impl_->header.data(), 5, &allocator) != SZ_OK)
			return std::unexpected(LzmaError::Allocate);
		impl_->allocated = true;
		LzmaDec_Init(&impl_->state);

		std::uint64_t size = 0;
		std::memcpy(&size, impl_->header.data() + 5, sizeof(size));
		if (size == unknownSize)
			return std::unexpected(LzmaError::Header);
		impl_->expected = size;
		impl_->remaining = size;
	}

	while (impl_->remaining != 0 && !input.empty())
	{
		SizeT srcLen = input.size();
		SizeT dstLen = static_cast<SizeT>((std::min)(static_cast<std::uint64_t>(impl_->out.size()),
		                                             impl_->remaining));
		const ELzmaFinishMode finish =
			static_cast<std::uint64_t>(dstLen) == impl_->remaining ? LZMA_FINISH_END
			                                                       : LZMA_FINISH_ANY;
		ELzmaStatus status{};
		if (LzmaDec_DecodeToBuf(&impl_->state, impl_->out.data(), &dstLen, input.data(), &srcLen,
		                        finish, &status) != SZ_OK)
			return std::unexpected(LzmaError::Corrupt);
		if (srcLen == 0 && dstLen == 0)
			return std::unexpected(LzmaError::Corrupt);
		if (dstLen != 0 && !sink(std::span<const std::uint8_t>(impl_->out.data(), dstLen)))
			return std::unexpected(LzmaError::Sink);

		impl_->remaining -= dstLen;
		input = input.subspan(srcLen);
	}
	return {};
}

bool LzmaDecoder::complete() const noexcept
{
	return impl_->headerFilled == headerSize && impl_->remaining == 0;
}

std::uint64_t LzmaDecoder::expectedSize() const noexcept
{
	return impl_->expected;
}

std::wstring_view describe(LzmaError error)
{
	switch (error)
	{
	case LzmaError::Header: return L"bad LZMA header";
	case LzmaError::Allocate: return L"LzmaDec_Allocate failed";
	case LzmaError::Corrupt: return L"corrupt LZMA stream";
	case LzmaError::Sink: return L"local write failed";
	}
	return L"unknown";
}

}
