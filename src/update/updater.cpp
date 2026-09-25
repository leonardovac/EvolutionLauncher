#include "update/updater.h"

#include "core/cancel.h"
#include "core/log.h"
#include "core/str.h"
#include "update/apply.h"
#include "update/http.h"
#include "update/lzma.h"
#include "update/purge.h"
#include "update/stale.h"

#include <thread>
#include <mutex>
#include <atomic>
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
                                                    std::wstring_view url, const RunContext& ctx)
{
	if (auto direct = Connection::open(session, url))
		return std::move(*direct);
	const std::wstring alternate = flipScheme(url);
	ctx.log(core::Level::Warn, std::format(L"connect to {} failed, trying {}", url, alternate));
	if (auto fallback = Connection::open(session, alternate))
		return std::move(*fallback);
	return std::unexpected(UpdateError::Connect);
}

std::expected<Index, UpdateError> fetchIndex(const Connection& origin, const RunContext& ctx)
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
	ctx.log(core::Level::Debug, std::format(L"index {}", path));
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

	const RunContext& ctx = options.ctx;

	const auto session = Session::open();
	if (!session)
		return std::unexpected(UpdateError::Session);

	const auto origin =
		openConnection(*session, originHost(options.config.title, options.config.branch), ctx);
	if (!origin)
		return std::unexpected(origin.error());

	const auto index = fetchIndex(*origin, ctx);
	if (!index)
		return std::unexpected(index.error());

	Summary summary;
	summary.entries = index->entries.size();
	summary.rejected = index->rejected.size();
	for (const std::wstring& line : index->rejected)
		ctx.log(core::Level::Debug, std::format(L"rejected line: {}", line));
	ctx.progress->onIndex(summary.entries, summary.rejected);

	std::vector<Entry> entries = index->entries;
	if (!options.only.empty())
	{
		const std::size_t before = entries.size();
		std::erase_if(entries, [&options](const Entry& entry)
		              { return !core::containsNoCase(entry.urlPath, options.only); });
		ctx.log(core::Level::Info, std::format(L"--only kept {} of {} entries", entries.size(),
		                               before));
	}

	if (const auto mainExeIt = std::ranges::find(entries, Category::MainExe, &Entry::category);
	    mainExeIt != entries.end())
		summary.mainExe = *mainExeIt;

	if (options.purgePrint)
	{
		const PurgeReport preview = runPurge(index->entries, options.config, /*dryRun*/ true, ctx);
		summary.purgeFiles = preview.wouldRemove.size();
		summary.purgeBytes = preview.bytes;
		summary.purgeFailed = preview.failures;
		summary.cancelled = preview.cancelled;
		return summary;
	}

	if (!options.staleReport)
	{
		const PurgeReport purge = runPurge(index->entries, options.config, /*dryRun*/ false, ctx);
		summary.purgeFiles = purge.removed.size();
		summary.purgeBytes = purge.bytes;
		summary.purgeFailed = purge.failures;
		ctx.log(core::Level::Info, std::format(L"{} unlisted files removed, {}", purge.removed.size(),
		                               core::widen(core::formatBytes(purge.bytes))));
		if (purge.cancelled)
		{
			summary.cancelled = true;
			return summary;
		}
	}

	ctx.log(core::Level::Info, std::format(L"checking {} against {}", branchName(options.config.branch),
	                               options.config.root.wstring()));
	// the resolved gates, so a run lines up with tools/check_index.py by eye
	ctx.log(core::Level::Info, std::format(L"lang={} steam={} eos={} dx12={} bulk={}",
	                               options.config.language, options.config.steam ? 1 : 0,
	                               options.config.eosSdk ? 1 : 0, options.config.dx12 ? 1 : 0,
	                               options.config.bulkDownload ? 1 : 0));
	const Plan plan = buildPlan(entries, options.config, ctx);
	summary.filtered = plan.filtered;
	summary.skipped = plan.skipped;
	summary.upToDate = plan.upToDate;
	summary.cacheDiffers = plan.cacheDiffers;
	summary.bulkSkipped = plan.bulkSkipped;
	summary.queued = plan.jobs.size();
	summary.downloadBytes = plan.downloadBytes;
	summary.queuedFiles.reserve(plan.jobs.size());
	for (const Job& job : plan.jobs)
		summary.queuedFiles.push_back({job.entry->installPath, job.entry->wireSize, job.reason});

	ctx.progress->onPlan(plan.jobs.size(), plan.downloadBytes, plan.filtered, plan.skipped,
	                     plan.upToDate, plan.bulkSkipped);

	if (ctx.cancelled())
	{
		summary.cancelled = true;
		return summary;
	}

	if (options.staleReport)
	{
		const StaleReport stale = findStale(entries, options.config, ctx);
		summary.staleFiles = stale.files.size();
		summary.staleBytes = stale.bytes;
		summary.cancelled = summary.cancelled || stale.cancelled;
		return summary;
	}

	if (options.dryRun)
	{
		for (const Job& job : plan.jobs)
			ctx.log(core::Level::Info, std::format(L"  {} {} [{}] {}", describe(job.reason),
			                               job.entry->installPath, describe(job.entry->category),
			                               core::widen(core::formatBytes(job.entry->wireSize))));
		return summary;
	}

	const auto content = openConnection(*session,
	                                    contentHost(options.config.title, options.config.branch,
	                                                options.config.forceHttps),
	                                    ctx);
	if (!content)
		return std::unexpected(content.error());

	const std::size_t jobs = std::clamp<std::size_t>(options.jobs, 1, maxJobs);
	std::atomic<std::size_t> position{0};
	std::mutex tally;

	// one category at a time: an interrupted run still leaves whole earlier categories done
	for (std::size_t first = 0; first < plan.jobs.size() && !summary.cancelled;)
	{
		std::size_t last = first;
		while (last < plan.jobs.size()
		       && plan.jobs[last].entry->category == plan.jobs[first].entry->category)
			++last;

		std::atomic<std::size_t> next{first};
		const auto worker = [&]
		{
			for (;;)
			{
				const std::size_t mine = next.fetch_add(1, std::memory_order_relaxed);
				if (mine >= last || ctx.cancelled())
					return;
				const Job& job = plan.jobs[mine];
				ctx.progress->onEntryStart(position.fetch_add(1, std::memory_order_relaxed) + 1,
				                           plan.jobs.size(), job.entry->installPath,
				                           job.entry->wireSize);
				// a chunkable entry spends the whole pool on itself; nothing else is in flight
				const auto applied =
					chunkable(*job.entry)
						? applyChunked(*content, *job.entry, options.config, ctx, jobs)
						: applyEntry(*content, *job.entry, options.config, ctx);

				const std::lock_guard lock(tally);
				if (!applied)
				{
					if (applied.error() == ApplyError::Cancelled)
					{
						summary.cancelled = true;
						return;
					}
					++summary.failed;
					ctx.progress->onEntryFailed(job.entry->installPath,
					                            describe(applied.error()));
					continue;
				}
				++summary.updated;
				summary.downloaded += applied->downloaded;
			}
		};

		// a chunked entry parallelises inside itself, so it must not race its own siblings
		const bool solo = std::any_of(plan.jobs.begin() + static_cast<std::ptrdiff_t>(first),
		                              plan.jobs.begin() + static_cast<std::ptrdiff_t>(last),
		                              [](const Job& job) { return chunkable(*job.entry); });
		const std::size_t count = solo ? 1 : std::min(jobs, last - first);

		std::vector<std::thread> pool;
		pool.reserve(count);
		for (std::size_t i = 0; i < count; ++i)
			pool.emplace_back(worker);
		for (std::thread& thread : pool)
			thread.join();

		if (ctx.cancelled())
			summary.cancelled = true;
		first = last;
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
