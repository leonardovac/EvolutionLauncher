#pragma once

#include "update/manifest.h"
#include "update/plan.h"
#include "update/progress.h"

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>

namespace wf
{

enum class UpdateError
{
	Session,
	Connect,
	IndexFetch,
	IndexDecode,
	IndexEmpty,
	NoRoot
};

struct Options
{
	Config config;
	bool dryRun = false;
	bool staleReport = false;
	bool purgePrint = false;
	std::wstring only;
	Progress* progress = nullptr;
};

struct Summary
{
	std::size_t entries = 0;
	std::size_t rejected = 0;
	std::size_t filtered = 0;
	std::size_t skipped = 0;
	std::size_t upToDate = 0;
	std::size_t queued = 0;
	std::size_t updated = 0;
	std::size_t failed = 0;
	std::uint64_t downloaded = 0;
	std::size_t staleFiles = 0;
	std::uint64_t staleBytes = 0;
	std::size_t purgeFiles = 0;
	std::uint64_t purgeBytes = 0;
	std::size_t purgeFailed = 0;
	bool cancelled = false;
	std::optional<Entry> mainExe;  // from the fetched index, for the caller to sideload-patch
};

std::expected<Summary, UpdateError> run(const Options& options);

std::wstring_view describe(UpdateError error);

}
