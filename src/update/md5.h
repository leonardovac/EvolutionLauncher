#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace wf
{

using Digest = std::array<std::uint8_t, 16>;

class Md5
{
public:
	Md5() noexcept { reset(); }

	void reset() noexcept;
	void update(std::span<const std::uint8_t> data) noexcept;
	[[nodiscard]] Digest finish() noexcept;

private:
	void transform(const std::uint8_t* block) noexcept;

	std::array<std::uint32_t, 4> state_{};
	std::array<std::uint8_t, 64> buffer_{};
	std::uint64_t length_ = 0;
};

std::expected<Digest, std::uint32_t> md5File(const std::filesystem::path& path);

std::string toHex(const Digest& digest);
std::optional<Digest> parseDigest(std::string_view text);

}
