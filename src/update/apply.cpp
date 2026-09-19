#include "update/apply.h"

#include "core/cancel.h"
#include "core/log.h"
#include "core/str.h"
#include "core/win.h"
#include "update/lzma.h"
#include "update/md5.h"

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
