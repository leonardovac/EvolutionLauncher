#include "update/purge.h"

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

std::wstring parentDir(std::wstring_view key)
{
	const std::size_t slash = key.find_last_of(L'\\');
	return slash == std::wstring_view::npos ? std::wstring() : std::wstring(key.substr(0, slash));
}

}

PurgeReport runPurge(std::span<const Entry> entries, const Config& config, bool dryRun,
                     Progress* progress)
{
	PurgeReport report;
	if (config.root.empty())
		return report;

	std::unordered_set<std::wstring> known;
	std::unordered_set<std::wstring> populatedDirs;
	known.reserve(entries.size());
	for (const Entry& entry : entries)
	{
		const std::wstring key = normalise(entry.installPath);
		known.insert(key);
		const std::wstring dir = parentDir(key);
		// the root itself is always protected; only sub-directories the index fills are eligible
		if (!dir.empty())
			populatedDirs.insert(dir);
	}

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
		const std::filesystem::path relative = it->path().lexically_relative(config.root);
		const std::wstring key = normalise(relative.wstring());
		if (key.empty() || key.starts_with(L"..") || key.ends_with(L".tmp") ||
		    key == L"defrag.log" || known.contains(key))
			continue;
		const std::wstring dir = parentDir(key);
		if (dir.empty() || !populatedDirs.contains(dir))
			continue;  // root-level or a dir the index never fills: protected
		if (config.launcher.shouldKeep(key))
			continue;

		const std::uintmax_t size = it->file_size(ec);
		const std::uint64_t bytes = ec ? 0u : static_cast<std::uint64_t>(size);
		ec.clear();

		if (dryRun)
		{
			report.wouldRemove.push_back(relative);
			report.bytes += bytes;
			if (progress != nullptr)
				progress->onStale(relative.wstring(), bytes);
			continue;
		}

		std::filesystem::remove(it->path(), ec);
		if (ec)
		{
			core::warn("could not remove {}: {}", core::narrow(key), ec.message());
			++report.failures;
			ec.clear();
			continue;
		}
		core::info("removed unlisted {}", core::narrow(key));
		report.removed.push_back(relative);
		report.bytes += bytes;
		if (progress != nullptr)
			progress->onStale(relative.wstring(), bytes);
	}
	return report;
}

}
