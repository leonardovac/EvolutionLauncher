#pragma once

#include "update/manifest.h"
#include "update/plan.h"
#include "update/progress.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace wf
{

struct PurgeReport
{
	std::vector<std::filesystem::path> removed;
	std::vector<std::filesystem::path> wouldRemove;
	std::uint64_t bytes = 0;
	std::size_t failures = 0;
	bool cancelled = false;
};

// deletes unlisted files only inside index-populated non-root dirs, never a kept path
PurgeReport runPurge(std::span<const Entry> entries, const Config& config, bool dryRun,
                     const RunContext& ctx);

}
