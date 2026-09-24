#include "update/purge.h"

#include "core/cancel.h"
#include "core/log.h"
#include "core/str.h"

#include <format>
#include <algorithm>
#include <array>
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

// the stock launcher's vestigial set (docs/protocol.md); nothing else qualifies
constexpr std::array<std::wstring_view, 7> purgeableExtensions{L".exe", L".dll", L".dat",
                                                               L".bin", L".pak", L".zip",
                                                               L".dmp"};
constexpr std::array<std::wstring_view, 2> purgeableFragments{L"charactercodescachedx", L"dx9"};
// stock spares the first three; `.tmp` is ours, a partial fetch the resume path still needs
constexpr std::array<std::wstring_view, 4> spared{L".texture.", L".texture_", L"unins00",
                                                  L".tmp"};

bool purgeable(std::wstring_view key)
{
	if (std::ranges::any_of(spared, [key](std::wstring_view f) { return key.contains(f); }))
		return false;
	if (std::ranges::any_of(purgeableExtensions,
	                        [key](std::wstring_view e) { return key.ends_with(e); }))
		return true;
	return std::ranges::any_of(purgeableFragments,
	                           [key](std::wstring_view f) { return key.contains(f); });
}

}

PurgeReport runPurge(std::span<const Entry> entries, const Config& config, bool dryRun,
                     const RunContext& ctx)
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
		ctx.log(core::Level::Warn, std::format(L"could not walk {}: {}", config.root.wstring(),
		                              core::widen(ec.message())));
		return report;
	}

	const std::filesystem::recursive_directory_iterator end;
	for (; it != end; it.increment(ec))
	{
		if (ec)
		{
			ctx.log(core::Level::Warn, std::format(L"walk error: {}", core::widen(ec.message())));
			ec.clear();
			continue;
		}
		if (ctx.cancelled())
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
		if (key.empty() || key.starts_with(L"..") || key == L"defrag.log" || known.contains(key))
			continue;
		if (!purgeable(key))
			continue;
		const std::wstring dir = parentDir(key);
		if (dir.empty() || !populatedDirs.contains(dir))
			continue;  // root-level or a dir the index never fills: protected
		if (config.launcher.isProtected(key))
			continue;

		const std::uintmax_t size = it->file_size(ec);
		const std::uint64_t bytes = ec ? 0u : static_cast<std::uint64_t>(size);
		ec.clear();

		if (dryRun)
		{
			report.wouldRemove.push_back(relative);
			report.bytes += bytes;
			ctx.progress->onStale(relative.wstring(), bytes);
			continue;
		}

		std::filesystem::remove(it->path(), ec);
		if (ec)
		{
			ctx.log(core::Level::Warn, std::format(L"could not remove {}: {}", key,
		                              core::widen(ec.message())));
			++report.failures;
			ec.clear();
			continue;
		}
		ctx.log(core::Level::Info, std::format(L"removed unlisted {}", key));
		report.removed.push_back(relative);
		report.bytes += bytes;
		ctx.progress->onStale(relative.wstring(), bytes);
	}

	// a skipped path is one the user does not want fetched, so it does not stay here either
	for (const std::wstring& skip : config.launcher.excludedPaths())
	{
		if (ctx.cancelled())
		{
			report.cancelled = true;
			break;
		}
		if (skip.empty() || (config.launcher.isProtected(skip) && !LauncherConfig::isAlwaysExcluded(skip)))
			continue;
		const std::filesystem::path target = (config.root / skip).lexically_normal();
		// the skip list is user input, so reject anything that climbs out of the root
		if (target.lexically_relative(config.root).wstring().starts_with(L".."))
			continue;
		if (!std::filesystem::is_regular_file(target, ec))
		{
			ec.clear();
			continue;
		}
		const std::uintmax_t size = std::filesystem::file_size(target, ec);
		const std::uint64_t bytes = ec ? 0u : static_cast<std::uint64_t>(size);
		ec.clear();

		if (dryRun)
		{
			report.wouldRemove.push_back(skip);
			report.bytes += bytes;
			ctx.progress->onStale(skip, bytes);
			continue;
		}

		std::filesystem::remove(target, ec);
		if (ec)
		{
			ctx.log(core::Level::Warn, std::format(L"could not remove {}: {}", skip,
		                              core::widen(ec.message())));
			++report.failures;
			ec.clear();
			continue;
		}
		ctx.log(core::Level::Info, std::format(L"removed excluded {}", skip));
		report.removed.push_back(skip);
		report.bytes += bytes;
		ctx.progress->onStale(skip, bytes);
	}
	return report;
}

}
