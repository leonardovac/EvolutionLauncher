#include "update/ranges.h"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <string>
#include <system_error>

namespace wf
{

std::filesystem::path RangeLog::pathFor(const std::filesystem::path& temporary)
{
	return std::filesystem::path(temporary).concat(L".ranges");
}

std::optional<RangeLog> RangeLog::load(const std::filesystem::path& temporary)
{
	const std::filesystem::path path = pathFor(temporary);
	std::error_code ec;
	if (!std::filesystem::exists(path, ec))
		return RangeLog{};

	std::ifstream file(path);
	if (!file)
		return std::nullopt;

	RangeLog log;
	std::string line;
	while (std::getline(file, line))
	{
		if (line.empty())
			continue;
		const std::size_t dash = line.find('-');
		if (dash == std::string::npos)
			return std::nullopt;
		Range range;
		const char* begin = line.data();
		const auto firstEnd = std::from_chars(begin, begin + dash, range.first);
		if (firstEnd.ec != std::errc{} || firstEnd.ptr != begin + dash)
			return std::nullopt;
		const char* tail = begin + dash + 1;
		const auto lastEnd = std::from_chars(tail, begin + line.size(), range.last);
		if (lastEnd.ec != std::errc{} || lastEnd.ptr != begin + line.size())
			return std::nullopt;
		if (range.last < range.first)
			return std::nullopt;
		log.done_.push_back(range);
	}
	return log;
}

bool RangeLog::covers(const Range& range) const
{
	const auto holds = [&range](const Range& have)
	{ return have.first <= range.first && have.last >= range.last; };
	return std::ranges::any_of(done_, holds);
}

std::vector<Range> RangeLog::gaps(std::uint64_t size, std::uint64_t chunkSize) const
{
	std::vector<Range> wanted;
	if (size == 0 || chunkSize == 0)
		return wanted;
	for (std::uint64_t at = 0; at < size; at += chunkSize)
	{
		const Range piece{at, std::min(at + chunkSize, size) - 1};
		if (!covers(piece))
			wanted.push_back(piece);
	}
	return wanted;
}

bool RangeLog::append(const std::filesystem::path& temporary, const Range& range)
{
	std::ofstream file(pathFor(temporary), std::ios::app);
	if (!file)
		return false;
	file << range.first << '-' << range.last << '\n';
	file.flush();
	if (!file)
		return false;
	done_.push_back(range);
	return true;
}

void removeRangeLog(const std::filesystem::path& temporary)
{
	std::error_code ec;
	std::filesystem::remove(RangeLog::pathFor(temporary), ec);
}

}
