#pragma once

#include "update/http.h"
#include "update/manifest.h"
#include "update/plan.h"
#include "update/progress.h"

#include <cstddef>

#include <cstdint>
#include <expected>
#include <string_view>

namespace wf
{

enum class ApplyError
{
	CreateTemp,
	Write,
	Http,
	Lzma,
	Truncated,
	HashMismatch,
	Move,
	Cancelled
};

struct ApplyResult
{
	std::uint64_t written = 0;
	std::uint64_t downloaded = 0;
};

// bulk entries at or above this are fetched as ranged chunks by several workers at once
inline constexpr std::uint64_t chunkThreshold = 256ull << 20;
inline constexpr std::uint64_t chunkSize = 256ull << 20;

[[nodiscard]] bool chunkable(const Entry& entry) noexcept;

std::expected<ApplyResult, ApplyError> applyEntry(const Connection& content, const Entry& entry,
                                                  const Config& config, const RunContext& ctx);

// Fetches one chunkable entry with `workers` threads sharing a sparse `.tmp`, then hashes the
// finished file. Cancellation deliberately leaves the `.tmp` and its range log behind.
std::expected<ApplyResult, ApplyError> applyChunked(const Connection& content, const Entry& entry,
                                                    const Config& config, const RunContext& ctx,
                                                    std::size_t workers);

std::wstring_view describe(ApplyError error);

}
