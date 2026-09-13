#include "update/updater.h"

#include "core/cancel.h"
#include "core/log.h"
#include "core/str.h"
#include "update/apply.h"
#include "update/http.h"
#include "update/lzma.h"
#include "update/purge.h"
#include "update/stale.h"

#include <algorithm>
#include <format>
#include <random>
#include <string>
#include <vector>

namespace wf
{
namespace
{

std::wstring flipScheme(std::wstring_view url)
{
	if (core::startsWithNoCase(url, L"https:"))
		return L"http:" + std::wstring(url.substr(6));
	if (core::startsWithNoCase(url, L"http:"))
		return L"https:" + std::wstring(url.substr(5));
	return std::wstring(url);
}

std::wstring indexPath()
{
	std::random_device device;
	std::mt19937 engine(device());
	std::uniform_int_distribution<std::uint32_t> nonce(0, 0xFFFFFFFFu);
	return std::format(L"/origin/{:08X}/index.txt.lzma", nonce(engine));
}

std::expected<Connection, UpdateError> openConnection(const Session& session,
                                                    std::wstring_view url)
{
	if (auto direct = Connection::open(session, url))
		return std::move(*direct);
	const std::wstring alternate = flipScheme(url);
	core::warn("connect to {} failed, trying {}", core::narrow(url), core::narrow(alternate));
	if (auto fallback = Connection::open(session, alternate))
		return std::move(*fallback);
	return std::unexpected(UpdateError::Connect);
}

std::expected<Index, UpdateError> fetchIndex(const Connection& origin)
{
	std::string decoded;
	LzmaDecoder lzma;
	bool decodeFailed = false;

	const auto emit = [&decoded](std::span<const std::uint8_t> bytes)
	{
		decoded.append(reinterpret_cast<const char*>(bytes.data()), bytes.size());
		return true;
	};

	const auto sink = [&](std::span<const std::uint8_t> bytes)
	{
		const auto pushed = lzma.push(bytes, emit);
		if (!pushed)
		{
			decodeFailed = true;
			return false;
		}
		return true;
	};

	const std::wstring path = indexPath();
	core::debug("index {}", core::narrow(path));
	if (!origin.fetch(path, 0, sink))
		return std::unexpected(decodeFailed ? UpdateError::IndexDecode : UpdateError::IndexFetch);
	if (!lzma.complete())
		return std::unexpected(UpdateError::IndexDecode);

	Index index = parseIndex(core::widen(decoded));
	if (index.entries.empty())
		return std::unexpected(UpdateError::IndexEmpty);
	return index;
}

}

std::expected<Summary, UpdateError> run(const Options& options)
{
	if (options.config.root.empty())
		return std::unexpected(UpdateError::NoRoot);

	Progress* progress = options.progress;

	const auto session = Session::open();
	if (!session)
		return std::unexpected(UpdateError::Session);

	const auto origin = openConnection(*session, originHost(options.config.branch));
	if (!origin)
		return std::unexpected(origin.error());

	const auto index = fetchIndex(*origin);
	if (!index)
		return std::unexpected(index.error());

	Summary summary;
	summary.entries = index->entries.size();
	summary.rejected = index->rejected.size();
	for (const std::wstring& line : index->rejected)
		core::debug("rejected line: {}", core::narrow(line));
	core::info("index: {} entries, {} rejected", summary.entries, summary.rejected);
	if (progress != nullptr)
		progress->onIndex(summary.entries, summary.rejected);

	std::vector<Entry> entries = index->entries;
	if (!options.only.empty())
	{
		const std::size_t before = entries.size();
		std::erase_if(entries, [&options](const Entry& entry)
		              { return !core::containsNoCase(entry.urlPath, options.only); });
		core::info("--only kept {} of {} entries", entries.size(), before);
	}

	if (const auto mainExeIt = std::ranges::find(entries, Category::MainExe, &Entry::category);
	    mainExeIt != entries.end())
		summary.mainExe = *mainExeIt;

	if (options.purgePrint)
	{
		const PurgeReport preview = runPurge(index->entries, options.config, /*dryRun*/ true, progress);
		summary.purgeFiles = preview.wouldRemove.size();
		summary.purgeBytes = preview.bytes;
		summary.purgeFailed = preview.failures;
		summary.cancelled = preview.cancelled;
		return summary;
	}

	if (!options.staleReport)
	{
		const PurgeReport purge = runPurge(index->entries, options.config, /*dryRun*/ false, progress);
		summary.purgeFiles = purge.removed.size();
		summary.purgeBytes = purge.bytes;
		summary.purgeFailed = purge.failures;
		core::info("{} unlisted files removed, {}", purge.removed.size(),
		           core::formatBytes(purge.bytes));
		if (purge.cancelled)
		{
			summary.cancelled = true;
			return summary;
		}
	}

	core::info("checking {} against {}", core::narrow(branchName(options.config.branch)),
	           options.config.root.string());
	const Plan plan = buildPlan(entries, options.config, progress);
	summary.filtered = plan.filtered;
	summary.skipped = plan.skipped;
	summary.upToDate = plan.upToDate;
	summary.cacheDiffers = plan.cacheDiffers;
	summary.queued = plan.jobs.size();

	core::info("{} filtered, {} skipped, {} up to date, {} queued ({} to download)", plan.filtered,
	           plan.skipped, plan.upToDate, plan.jobs.size(), core::formatBytes(plan.downloadBytes));
	if (progress != nullptr)
		progress->onPlan(plan.jobs.size(), plan.downloadBytes);

	if (core::cancelled())
	{
		summary.cancelled = true;
		return summary;
	}

	if (options.staleReport)
	{
		const StaleReport stale = findStale(entries, options.config, progress);
		summary.staleFiles = stale.files.size();
		summary.staleBytes = stale.bytes;
		summary.cancelled = summary.cancelled || stale.cancelled;
		return summary;
	}

	if (options.dryRun)
	{
		for (const Job& job : plan.jobs)
			core::info("  {} {} [{}] {}", core::narrow(describe(job.reason)),
			           core::narrow(job.entry->installPath),
			           core::narrow(describe(job.entry->category)),
			           core::formatBytes(job.entry->wireSize));
		return summary;
	}

	const auto content = openConnection(*session, contentHost(options.config.branch,
	                                                         options.config.forceHttps));
	if (!content)
		return std::unexpected(content.error());

	std::size_t position = 0;
	for (const Job& job : plan.jobs)
	{
		if (core::cancelled())
		{
			summary.cancelled = true;
			break;
		}
		++position;
		core::info("[{}/{}] {} ({})", position, plan.jobs.size(),
		           core::narrow(job.entry->installPath), core::formatBytes(job.entry->wireSize));
		if (progress != nullptr)
			progress->onEntryStart(position, plan.jobs.size(), job.entry->installPath,
			                       job.entry->wireSize);
		const auto applied = applyEntry(*content, *job.entry, options.config, progress);
		if (!applied)
		{
			if (applied.error() == ApplyError::Cancelled)
			{
				summary.cancelled = true;
				break;
			}
			++summary.failed;
			core::error("{}: {}", core::narrow(job.entry->installPath),
			            core::narrow(describe(applied.error())));
			if (progress != nullptr)
				progress->onEntryFailed(job.entry->installPath, describe(applied.error()));
			continue;
		}
		++summary.updated;
		summary.downloaded += applied->downloaded;
	}
	return summary;
}

std::wstring_view describe(UpdateError error)
{
	switch (error)
	{
	case UpdateError::Session: return L"could not open a WinHTTP session";
	case UpdateError::Connect: return L"could not reach the service";
	case UpdateError::IndexFetch: return L"index download failed";
	case UpdateError::IndexDecode: return L"index decompression failed";
	case UpdateError::IndexEmpty: return L"index parsed to zero entries";
	case UpdateError::NoRoot: return L"no install root";
	}
	return L"unknown";
}

}
