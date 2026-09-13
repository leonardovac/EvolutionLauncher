#include "update/md5.h"

#include "core/str.h"
#include "core/win.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cstring>
#include <vector>

namespace wf
{
namespace
{

constexpr std::array<std::uint32_t, 64> sines{
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};

constexpr std::array<std::uint32_t, 64> shifts{
    7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
    5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
    4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
    6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};

constexpr std::size_t readChunk = 1u << 20;

}

void Md5::reset() noexcept
{
	state_ = {0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u};
	buffer_.fill(0);
	length_ = 0;
}

void Md5::transform(const std::uint8_t* block) noexcept
{
	std::array<std::uint32_t, 16> words{};
	for (std::size_t i = 0; i < words.size(); ++i)
		std::memcpy(&words[i], block + i * 4, 4);

	std::uint32_t a = state_[0];
	std::uint32_t b = state_[1];
	std::uint32_t c = state_[2];
	std::uint32_t d = state_[3];

	for (std::uint32_t i = 0; i < 64; ++i)
	{
		std::uint32_t f = 0;
		std::uint32_t g = 0;
		if (i < 16)
		{
			f = (b & c) | (~b & d);
			g = i;
		}
		else if (i < 32)
		{
			f = (d & b) | (~d & c);
			g = (5 * i + 1) % 16;
		}
		else if (i < 48)
		{
			f = b ^ c ^ d;
			g = (3 * i + 5) % 16;
		}
		else
		{
			f = c ^ (b | ~d);
			g = (7 * i) % 16;
		}

		const std::uint32_t rotated = a + f + sines[i] + words[g];
		a = d;
		d = c;
		c = b;
		b += std::rotl(rotated, static_cast<int>(shifts[i]));
	}

	state_[0] += a;
	state_[1] += b;
	state_[2] += c;
	state_[3] += d;
}

void Md5::update(std::span<const std::uint8_t> data) noexcept
{
	std::size_t used = static_cast<std::size_t>(length_ % 64);
	length_ += data.size();

	if (used != 0)
	{
		const std::size_t fill = (std::min)(data.size(), 64 - used);
		std::memcpy(buffer_.data() + used, data.data(), fill);
		data = data.subspan(fill);
		used += fill;
		if (used < 64)
			return;
		transform(buffer_.data());
	}

	while (data.size() >= 64)
	{
		transform(data.data());
		data = data.subspan(64);
	}

	if (!data.empty())
		std::memcpy(buffer_.data(), data.data(), data.size());
}

Digest Md5::finish() noexcept
{
	const std::uint64_t bits = length_ * 8;
	const std::size_t used = static_cast<std::size_t>(length_ % 64);
	const std::size_t padding = used < 56 ? 56 - used : 120 - used;

	std::array<std::uint8_t, 72> tail{};
	tail[0] = 0x80;
	std::memcpy(tail.data() + padding, &bits, sizeof(bits));
	update(std::span<const std::uint8_t>(tail.data(), padding + sizeof(bits)));

	Digest out{};
	for (std::size_t i = 0; i < state_.size(); ++i)
		std::memcpy(out.data() + i * 4, &state_[i], 4);
	return out;
}

std::expected<Digest, std::uint32_t> md5File(const std::filesystem::path& path)
{
	core::File file(::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
	                              OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
	if (!file)
		return std::unexpected(::GetLastError());

	Md5 md5;
	std::vector<std::uint8_t> chunk(readChunk);
	for (;;)
	{
		DWORD read = 0;
		if (!::ReadFile(file.get(), chunk.data(), static_cast<DWORD>(chunk.size()), &read, nullptr))
			return std::unexpected(::GetLastError());
		if (read == 0)
			break;
		md5.update(std::span<const std::uint8_t>(chunk.data(), read));
	}
	return md5.finish();
}

std::string toHex(const Digest& digest)
{
	std::string out = core::hex(digest);
	std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return out;
}

std::optional<Digest> parseDigest(std::string_view text)
{
	return core::parseHash(core::widen(text));
}

}
