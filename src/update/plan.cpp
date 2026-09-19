#include "update/plan.h"

#include "core/cancel.h"
#include "core/log.h"
#include "core/str.h"
#include "update/md5.h"

#include <algorithm>
#include <array>
#include <ranges>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace wf
{
namespace
{

constexpr std::uint64_t progressStride = 4ull << 30;

// only these three extensions carry a _<lang> suffix in the index
constexpr std::array<std::wstring_view, 3> localizedExtensions{L".cache", L".toc", L".rtf"};

std::wstring_view extensionOf(std::wstring_view installPath)
{
	const std::size_t dot = installPath.rfind(L'.');
	return dot == std::wstring_view::npos ? std::wstring_view{} : installPath.substr(dot);
}

bool languageAllows(std::wstring_view installPath, std::wstring_view language)
{
	const std::wstring_view extension = extensionOf(installPath);
	const auto matches = [extension](std::wstring_view candidate)
	{ return core::equalsNoCase(extension, candidate); };
	if (!std::ranges::any_of(localizedExtensions, matches))
		return true;

	const std::wstring_view stem = installPath.substr(0, installPath.size() - extension.size());
	if (stem.size() <= 3)
		return true;
	const std::wstring_view tail = stem.substr(stem.size() - 3);
	if (tail.front() != L'_')
		return true;

	const std::wstring_view code = tail.substr(1);
	if (core::equalsNoCase(code, L"xx") || core::equalsNoCase(code, language))
		return true;
	if (!core::containsNoCase(stem, L"misc"))
		return false;
	return core::equalsNoCase(code, L"en");
}

std::wstring key(std::wstring_view installPath)
{
	return core::lower(installPath);
}

using HostTable = std::array<std::array<std::wstring_view, 3>, 2>;

constexpr HostTable originHosts{
	{{L"https://origin.warframe.com", L"https://origin-test.warframe.com",
	  L"https://origin-dev.warframe.com"},
	 {L"https://origin.soulframe.com", L"https://origin-test.soulframe.com",
	  L"https://origin-dev.soulframe.com"}}};

constexpr HostTable secureContentHosts{
	{{L"https://content.warframe.com", L"https://content-test.warframe.com",
	  L"https://content-dev.warframe.com"},
	 {L"https://content.soulframe.com", L"https://content-test.soulframe.com",
	  L"https://content-dev.soulframe.com"}}};

constexpr HostTable plainContentHosts{
	{{L"http://content.warframe.com", L"http://content-test.warframe.com",
	  L"http://content-dev.warframe.com"},
	 {L"http://content.soulframe.com", L"http://content-test.soulframe.com",
	  L"http://content-dev.soulframe.com"}}};

constexpr std::wstring_view host(const HostTable& table, Title title, Branch branch)
{
	const auto row = static_cast<std::size_t>(title);
	const auto column = static_cast<std::size_t>(branch);
	return table[row < table.size() ? row : 0][column < 3 ? column : 0];
}

}

std::wstring_view originHost(Title title, Branch branch)
{
	return host(originHosts, title, branch);
}

std::wstring_view contentHost(Title title, Branch branch, bool forceHttps)
{
	return host(forceHttps ? secureContentHosts : plainContentHosts, title, branch);
}

std::wstring_view branchName(Branch branch)
{
	switch (branch)
	{
	case Branch::Test: return L"Test";
	case Branch::Dev: return L"Dev";
	case Branch::Public: break;
	}
	return L"Public";
}

bool appliesToClient(const Entry& entry, const Config& config)
{
	if (!config.steam && core::containsNoCase(entry.urlPath, L"/steam"))
		return false;
	if (!config.eosSdk && core::containsNoCase(entry.urlPath, L"/eossdk"))
		return false;
	if (config.branch == Branch::Public && core::containsNoCase(entry.urlPath, L"d3d12sdklayers"))
		return false;
	if (!config.dx12)
	{
		constexpr std::array<std::wstring_view, 2> dx12{L"dx12.toc", L"dx12.cache"};
		const auto hits = [&entry](std::wstring_view marker)
		{ return core::containsNoCase(entry.urlPath, marker); };
		if (std::ranges::any_of(dx12, hits))
			return false;
	}
	return languageAllows(entry.installPath, config.language);
}

Plan buildPlan(std::span<const Entry> entries, const Config& config, Progress* progress)
{
	Plan plan;
	std::unordered_map<std::wstring, const Entry*> byPath;
	std::unordered_set<std::wstring> queued;
	byPath.reserve(entries.size());

	std::vector<const Entry*> applicable;
	applicable.reserve(entries.size());
	for (const Entry& entry : entries)
	{
		if (!appliesToClient(entry, config))
		{
			++plan.filtered;
			continue;
		}
		if (config.launcher.isExcluded(entry.installPath))
		{
			++plan.skipped;
			core::debug("skipping {}", core::narrow(entry.installPath));
			continue;
		}
		applicable.push_back(&entry);
		byPath.emplace(key(entry.installPath), &entry);
	}

	std::uint64_t hashedBytes = 0;
	std::uint64_t reported = 0;
	std::size_t checked = 0;
	for (const Entry* entry : applicable)
	{
		if (core::cancelled())
			break;
		++checked;
		if (progress != nullptr)
			progress->onChecking(checked, applicable.size(), hashedBytes);

		const std::filesystem::path destination = config.root / entry->installPath;
		std::error_code ec;
		if (!std::filesystem::exists(destination, ec))
		{
			plan.jobs.push_back({entry, Reason::Missing});
			queued.insert(key(entry->installPath));
			continue;
		}

		if (entry->category == Category::CacheOrToc && !config.hashCaches)
		{
			++plan.upToDate;
			continue;
		}

		++plan.hashed;
		const auto digest = md5File(destination);
		hashedBytes += std::filesystem::file_size(destination, ec);
		if (hashedBytes - reported >= progressStride)
		{
			reported = hashedBytes;
			core::info("checked {}/{}, hashed {}", checked, applicable.size(),
			           core::formatBytes(hashedBytes));
		}

		if (digest && *digest == entry->hash)
		{
			++plan.upToDate;
			continue;
		}
		const PatchRecord* record =
			config.sideload ? config.launcher.patchFor(entry->installPath) : nullptr;
		if (record != nullptr && digest && *digest == record->result &&
		    record->source == entry->hash)
		{
			++plan.upToDate;
			continue;
		}
		if (!digest)
			core::warn("could not read {} (0x{:08X})", core::narrow(entry->installPath),
			           digest.error());
		plan.jobs.push_back({entry, Reason::HashMismatch});
		queued.insert(key(entry->installPath));
	}

	// the defragmenter rewrites the cache in place, so its bytes stop matching the index hash
	// for good; a present-but-different cache file is reported, never re-fetched
	if (config.hashCaches)
	{
		const auto dropped = std::ranges::remove_if(plan.jobs, [](const Job& job) {
			return job.entry->category == Category::CacheOrToc &&
			       job.reason == Reason::HashMismatch;
		});
		const auto count = static_cast<std::size_t>(std::ranges::distance(dropped));
		if (count != 0)
		{
			core::warn("{} cache files differ from the index: that is what defragmenting does, "
			           "and re-fetching them would only undo it, so they are left alone",
			           count);
			plan.cacheDiffers = count;
			for (const Job& job : dropped)
				queued.erase(key(job.entry->installPath));
			plan.jobs.erase(dropped.begin(), dropped.end());
		}
	}

	// a .cache and its .toc must move together or the pair is inconsistent on disk
	for (std::size_t i = 0, count = plan.jobs.size(); i < count; ++i)
	{
		const Entry& entry = *plan.jobs[i].entry;
		if (entry.category != Category::CacheOrToc)
			continue;
		const std::wstring_view extension = extensionOf(entry.installPath);
		const std::wstring_view stem =
			std::wstring_view(entry.installPath).substr(0, entry.installPath.size() - extension.size());
		const std::wstring_view other =
			core::equalsNoCase(extension, L".cache") ? std::wstring_view(L".toc")
			                                         : std::wstring_view(L".cache");
		const std::wstring sibling = key(std::wstring(stem) + std::wstring(other));
		if (queued.contains(sibling))
			continue;
		const auto found = byPath.find(sibling);
		if (found == byPath.end())
			continue;
		plan.jobs.push_back({found->second, Reason::Sibling});
		queued.insert(sibling);
	}

	// category order, so an interrupted run still leaves a startable install
	std::ranges::stable_sort(plan.jobs, {}, [](const Job& job) { return job.entry->category; });

	for (const Job& job : plan.jobs)
		plan.downloadBytes += job.entry->wireSize;
	return plan;
}

std::wstring_view describe(Reason reason)
{
	switch (reason)
	{
	case Reason::Missing: return L"missing";
	case Reason::HashMismatch: return L"changed";
	case Reason::Sibling: return L"pair";
	}
	return L"?";
}

}
