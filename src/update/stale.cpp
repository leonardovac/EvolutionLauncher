#include "update/stale.h"

#include "core/cancel.h"
#include "core/log.h"
#include "core/str.h"

#include <algorithm>
#include <string>
#include <system_error>
#include <unordered_set>

namespace wf
{
namespace
{

std::wstring normalise(std::wstring_view path)
{
	std::wstring out = core::lower(path);
	std::ranges::replace(out, L'/', L'\\');
	if (!out.empty() && out.front() == L'\\')
		out.erase(out.begin());
	return out;
}

}

StaleReport findStale(std::span<const Entry> entries, const Config& config, Progress* progress)
{
	StaleReport report;
	if (config.root.empty())
		return report;

	std::unordered_set<std::wstring> known;
	known.reserve(entries.size());
	for (const Entry& entry : entries)
		known.insert(normalise(entry.installPath));

	std::error_code ec;
	std::filesystem::recursive_directory_iterator it(
		config.root, std::filesystem::directory_options::skip_permission_denied, ec);
	if (ec)
	{
		core::warn("could not walk {}: {}", config.root.string(), ec.message());
		return report;
	}

	const std::filesystem::recursive_directory_iterator end;
	for (; it != end; it.increment(ec))
	{
		if (ec)
		{
			core::warn("walk error: {}", ec.message());
			ec.clear();
			continue;
		}
		if (core::cancelled())
		{
			report.cancelled = true;
			break;
		}
		if (!it->is_regular_file(ec) || ec)
		{
			ec.clear();
			continue;
		}
		const std::filesystem::path relative = std::filesystem::relative(it->path(), config.root, ec);
		if (ec)
		{
			ec.clear();
			continue;
		}
		const std::wstring key = normalise(relative.wstring());
		if (key.ends_with(L".tmp") || known.contains(key))
			continue;
		const std::uintmax_t size = it->file_size(ec);
		const std::uint64_t bytes = ec ? 0u : static_cast<std::uint64_t>(size);
		ec.clear();
		report.files.push_back(relative);
		report.bytes += bytes;
		if (progress != nullptr)
			progress->onStale(relative.wstring(), bytes);
	}
	return report;
}

}
