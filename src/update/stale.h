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

struct StaleReport
{
	std::vector<std::filesystem::path> files;
	std::uint64_t bytes = 0;
	bool cancelled = false;
};

// files under the branch root that the index does not name; nothing is ever removed
StaleReport findStale(std::span<const Entry> entries, const Config& config,
                      const RunContext& ctx);

}
