#include "update/apply.h"

#include "update/md5.h"
#include "update/ranges.h"

#include "core/cancel.h"
#include "core/log.h"
#include "core/str.h"
#include "core/win.h"
#include "update/lzma.h"
#include "update/md5.h"

#include <numeric>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <format>
#include <windows.h>

#include <optional>
#include <system_error>

namespace wf
{
namespace
{

constexpr int attempts = 3;

}

std::expected<ApplyResult, ApplyError> applyEntry(const Connection& content, const Entry& entry,
                                                  const Config& config, const RunContext& ctx)
{
	const std::filesystem::path destination = config.root / entry.installPath;
	std::error_code ec;
	std::filesystem::create_directories(destination.parent_path(), ec);

	std::filesystem::path temporary = destination;
	temporary += L".tmp";

	core::File file(::CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
	                              FILE_ATTRIBUTE_NORMAL, nullptr));
	if (!file)
		return std::unexpected(ApplyError::CreateTemp);

	Md5 md5;
	LzmaDecoder lzma;
	std::uint64_t received = 0;
	std::uint64_t written = 0;
	bool writeFailed = false;
	std::optional<LzmaError> lzmaFailure;

	const auto emit = [&](std::span<const std::uint8_t> bytes)
	{
		DWORD done = 0;
		if (!::WriteFile(file.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &done, nullptr)
		    || done != bytes.size())
		{
			writeFailed = true;
			return false;
		}
		md5.update(bytes);
		written += bytes.size();
		return true;
	};

	const auto sink = [&](std::span<const std::uint8_t> bytes)
	{
		if (ctx.cancelled())
			return false;
		received += bytes.size();
		ctx.progress->onBytes(bytes.size());
		if (entry.compression == Compression::Bulk)
			return emit(bytes);
		const auto pushed = lzma.push(bytes, emit);
		if (!pushed)
		{
			lzmaFailure = pushed.error();
			return false;
		}
		return true;
	};

	const auto fail = [&](ApplyError error) -> std::expected<ApplyResult, ApplyError>
	{
		file.reset();
		std::error_code removeError;
		std::filesystem::remove(temporary, removeError);
		return std::unexpected(error);
	};

	for (int attempt = 0;; ++attempt)
	{
		// resuming at the compressed offset keeps the decoder and digest state valid
		const auto fetched = content.fetch(entry.urlPath, received, sink);
		if (fetched)
			break;
		if (ctx.cancelled())
			return fail(ApplyError::Cancelled);
		if (lzmaFailure)
			return fail(ApplyError::Lzma);
		if (writeFailed)
			return fail(ApplyError::Write);
		if (permanent(fetched.error()) || attempt + 1 >= attempts)
			return fail(ApplyError::Http);
		ctx.log(core::Level::Debug, std::format(L"retrying {} at {} ({})", entry.installPath, received,
		                               describe(fetched.error())));
	}

	if (received != entry.wireSize)
		return fail(ApplyError::Truncated);
	if (entry.compression == Compression::Lzma && !lzma.complete())
		return fail(ApplyError::Truncated);
	if (md5.finish() != entry.hash)
		return fail(ApplyError::HashMismatch);

	file.reset();
	if (!::MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING)
	    && !::ReplaceFileW(destination.c_str(), temporary.c_str(), nullptr, 0, nullptr, nullptr))
	{
		std::error_code removeError;
		std::filesystem::remove(temporary, removeError);
		return std::unexpected(ApplyError::Move);
	}

	return ApplyResult{written, received};
}

bool chunkable(const Entry& entry) noexcept
{
	return entry.compression == Compression::Bulk && entry.wireSize >= chunkThreshold;
}

std::expected<ApplyResult, ApplyError> applyChunked(const Connection& content, const Entry& entry,
                                                    const Config& config, const RunContext& ctx,
                                                    std::size_t workers)
{
	const std::filesystem::path destination = config.root / entry.installPath;
	std::error_code ec;
	std::filesystem::create_directories(destination.parent_path(), ec);

	std::filesystem::path temporary = destination;
	temporary += L".tmp";

	auto log = RangeLog::load(temporary);
	if (!log)
	{
		// an unreadable sidecar makes the .tmp meaningless, so both go and the entry restarts
		std::filesystem::remove(temporary, ec);
		removeRangeLog(temporary);
		log = RangeLog{};
	}

	{
		core::File create(::CreateFileW(temporary.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
		                                nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
		if (!create)
			return std::unexpected(ApplyError::CreateTemp);
		LARGE_INTEGER size{};
		size.QuadPart = static_cast<LONGLONG>(entry.wireSize);
		if (!::SetFilePointerEx(create.get(), size, nullptr, FILE_BEGIN)
		    || !::SetEndOfFile(create.get()))
			return std::unexpected(ApplyError::CreateTemp);
	}

	std::vector<Range> wanted = log->gaps(entry.wireSize, chunkSize);
	const std::uint64_t already = entry.wireSize - std::accumulate(
		wanted.begin(), wanted.end(), std::uint64_t{0},
		[](std::uint64_t sum, const Range& r) { return sum + (r.last - r.first + 1); });

	std::atomic<std::size_t> next{0};
	std::atomic<std::uint64_t> fetched{already};
	std::atomic<bool> failed{false};
	std::atomic<bool> cancelled{false};
	std::mutex logMutex;

	const auto pump = [&]
	{
		core::File file(::CreateFileW(temporary.c_str(), GENERIC_WRITE,
		                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
		                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
		if (!file)
		{
			failed.store(true, std::memory_order_relaxed);
			return;
		}
		for (;;)
		{
			const std::size_t mine = next.fetch_add(1, std::memory_order_relaxed);
			if (mine >= wanted.size() || failed.load(std::memory_order_relaxed)
			    || cancelled.load(std::memory_order_relaxed))
				return;
			const Range piece = wanted[mine];
			std::uint64_t at = piece.first;
			bool writeFailed = false;

			const auto sink = [&](std::span<const std::uint8_t> bytes)
			{
				if (ctx.cancelled())
				{
					cancelled.store(true, std::memory_order_relaxed);
					return false;
				}
				OVERLAPPED where{};
				where.Offset = static_cast<DWORD>(at & 0xFFFFFFFFull);
				where.OffsetHigh = static_cast<DWORD>(at >> 32);
				DWORD done = 0;
				if (!::WriteFile(file.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &done,
				                 &where)
				    || done != bytes.size())
				{
					writeFailed = true;
					return false;
				}
				at += bytes.size();
				fetched.fetch_add(bytes.size(), std::memory_order_relaxed);
				ctx.progress->onBytes(bytes.size());
				return true;
			};

			for (int attempt = 0;; ++attempt)
			{
				const auto got = content.fetch(entry.urlPath, at, sink, piece.last);
				if (got && at == piece.last + 1)
					break;
				if (cancelled.load(std::memory_order_relaxed))
					return;
				// a short range that the server called complete will never grow on a retry
				if (writeFailed || got || permanent(got.error()) || attempt + 1 >= attempts)
				{
					failed.store(true, std::memory_order_relaxed);
					return;
				}
			}

			const std::lock_guard lock(logMutex);
			log->append(temporary, piece);
		}
	};

	const std::size_t count = std::max<std::size_t>(1, std::min(workers, wanted.size()));
	std::vector<std::thread> pool;
	pool.reserve(count);
	for (std::size_t i = 0; i < count; ++i)
		pool.emplace_back(pump);
	for (std::thread& worker : pool)
		worker.join();

	// the rule's one exception: the .tmp and its log survive so the next run resumes
	if (cancelled.load(std::memory_order_relaxed) || ctx.cancelled())
		return std::unexpected(ApplyError::Cancelled);

	const auto drop = [&](ApplyError error) -> std::expected<ApplyResult, ApplyError>
	{
		std::filesystem::remove(temporary, ec);
		removeRangeLog(temporary);
		return std::unexpected(error);
	};

	if (failed.load(std::memory_order_relaxed))
		return std::unexpected(ApplyError::Http);

	const auto digest = md5File(temporary);
	if (!digest)
		return drop(ApplyError::Write);
	if (*digest != entry.hash)
		return drop(ApplyError::HashMismatch);

	if (!::MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING)
	    && !::ReplaceFileW(destination.c_str(), temporary.c_str(), nullptr, 0, nullptr, nullptr))
		return drop(ApplyError::Move);
	removeRangeLog(temporary);

	return ApplyResult{entry.wireSize, fetched.load(std::memory_order_relaxed) - already};
}

std::wstring_view describe(ApplyError error)
{
	switch (error)
	{
	case ApplyError::CreateTemp: return L"could not create the temporary file";
	case ApplyError::Write: return L"write failed";
	case ApplyError::Http: return L"download failed";
	case ApplyError::Lzma: return L"decompression failed";
	case ApplyError::Truncated: return L"short download";
	case ApplyError::HashMismatch: return L"hash mismatch";
	case ApplyError::Move: return L"could not replace the destination";
	case ApplyError::Cancelled: return L"cancelled";
	}
	return L"unknown";
}

}
