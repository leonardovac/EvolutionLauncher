#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace wf
{

struct Range
{
	std::uint64_t first = 0;   // inclusive
	std::uint64_t last = 0;    // inclusive
};

// Records which byte ranges of a chunked `.tmp` are already on disk. Chunks arrive out of
// order, so a byte count cannot describe the progress and a running digest is impossible.
class RangeLog
{
public:
	static std::filesystem::path pathFor(const std::filesystem::path& temporary);

	// nullopt when the file exists but does not parse; the caller then starts the entry over
	static std::optional<RangeLog> load(const std::filesystem::path& temporary);

	[[nodiscard]] bool covers(const Range& range) const;
	// the pieces of [0, size) this log does not already hold, split at `chunkSize`
	[[nodiscard]] std::vector<Range> gaps(std::uint64_t size, std::uint64_t chunkSize) const;

	bool append(const std::filesystem::path& temporary, const Range& range);

private:
	std::vector<Range> done_;
};

void removeRangeLog(const std::filesystem::path& temporary);

}
