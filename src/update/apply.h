#pragma once

#include "update/http.h"
#include "update/manifest.h"
#include "update/plan.h"

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

std::expected<ApplyResult, ApplyError> applyEntry(const Connection& content, const Entry& entry,
                                                  const Config& config);

std::wstring_view describe(ApplyError error);

}
