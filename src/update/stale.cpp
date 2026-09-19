#include "update/stale.h"

#include "core/cancel.h"
#include "core/log.h"
#include "core/str.h"

#include <format>
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

StaleReport findStale(std::span<const Entry> entries, const Config& config, const RunContext& ctx)
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
		// lexical, so a reparse point cannot report a path outside the root
		if (key.empty() || key.starts_with(L"..") || key.ends_with(L".tmp") ||
		    key == L"defrag.log" || known.contains(key))
			continue;
		const std::uintmax_t size = it->file_size(ec);
		const std::uint64_t bytes = ec ? 0u : static_cast<std::uint64_t>(size);
		ec.clear();
		report.files.push_back(relative);
		report.bytes += bytes;
		ctx.progress->onStale(relative.wstring(), bytes);
	}
	return report;
}

}
