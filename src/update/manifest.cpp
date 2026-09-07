#include "update/manifest.h"

#include "core/str.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <ranges>

namespace wf
{
namespace
{

constexpr std::wstring_view lzmaMarker = L".lzma,";
constexpr std::wstring_view bulkMarker = L".bulk,";

// mirrors the launcher's category-1 prefix table
constexpr std::array<std::wstring_view, 6> toolPrefixes{
    L"/Tools/Launcher.exe",           L"/Tools/RemoteCrashSender.exe",
    L"/Tools/Windows/x64/dbghelp.dll", L"/Tools/Windows/x64/symsrv.dll",
    L"/Tools/Windows/x64/EOSSDK",     L"/Tools/CEF3_1/"};

bool isUnsafe(std::wstring_view installPath)
{
	if (installPath.empty())
		return true;
	if (installPath.front() == L'\\')
		return true;
	if (installPath.contains(L':'))
		return true;
	for (const auto part : std::views::split(installPath, L'\\'))
	{
		const std::wstring_view segment(part.begin(), part.end());
		if (segment.empty() || segment == L"." || segment == L"..")
			return true;
	}
	return false;
}

Category classify(std::wstring_view urlPath, std::wstring_view installPath)
{
	if (core::containsNoCase(urlPath, L"/cef"))
		return Category::Cef;
	const auto matchesTool = [urlPath](std::wstring_view prefix)
	{ return core::startsWithNoCase(urlPath, prefix); };
	if (std::ranges::any_of(toolPrefixes, matchesTool) || core::containsNoCase(urlPath, L"/steam"))
		return Category::Tools;
	if (!installPath.contains(L'\\') && core::endsWithNoCase(installPath, L".exe"))
		return Category::MainExe;
	constexpr std::array<std::wstring_view, 2> cacheExtensions{L".cache", L".toc"};
	const auto hasExtension = [installPath](std::wstring_view extension)
	{ return core::endsWithNoCase(installPath, extension); };
	if (std::ranges::any_of(cacheExtensions, hasExtension))
		return Category::CacheOrToc;
	return Category::Data;
}

}

std::expected<Entry, LineError> parseLine(std::wstring_view line)
{
	Compression compression = Compression::Lzma;
	std::size_t marker = line.find(lzmaMarker);
	if (marker == std::wstring_view::npos)
	{
		compression = Compression::Bulk;
		marker = line.find(bulkMarker);
	}
	if (marker == std::wstring_view::npos || marker == 0)
		return std::unexpected(LineError::NoExtension);

	const std::size_t dot = line.rfind(L'.', marker - 1);
	if (dot == std::wstring_view::npos || marker != dot + 33)
		return std::unexpected(LineError::HashNotWhereExpected);

	const auto hash = core::parseHash(line.substr(dot + 1, 32));
	if (!hash)
		return std::unexpected(LineError::BadHash);

	const std::wstring_view sizeText = line.substr(marker + lzmaMarker.size());
	const std::string narrowSize = core::narrow(sizeText);
	std::uint64_t wireSize = 0;
	const auto* first = narrowSize.data();
	const auto* last = first + narrowSize.size();
	const auto parsed = std::from_chars(first, last, wireSize);
	if (parsed.ec != std::errc{} || parsed.ptr != last || narrowSize.empty())
		return std::unexpected(LineError::BadSize);

	Entry entry;
	entry.urlPath = line.substr(0, marker + 5);
	entry.installPath = line.substr(0, dot);
	std::ranges::replace(entry.installPath, L'/', L'\\');
	if (!entry.installPath.empty() && entry.installPath.front() == L'\\')
		entry.installPath.erase(entry.installPath.begin());
	if (isUnsafe(entry.installPath))
		return std::unexpected(LineError::UnsafePath);

	entry.hash = *hash;
	entry.wireSize = wireSize;
	entry.compression = compression;
	entry.category = classify(entry.urlPath, entry.installPath);
	return entry;
}

Index parseIndex(std::wstring_view text)
{
	Index index;
	for (const auto part : std::views::split(text, L'\n'))
	{
		std::wstring_view line(part.begin(), part.end());
		while (!line.empty() && (line.back() == L'\r' || line.back() == L' ' || line.back() == L'\t'))
			line.remove_suffix(1);
		if (line.empty())
			continue;
		if (auto entry = parseLine(line))
			index.entries.push_back(std::move(*entry));
		else
			index.rejected.emplace_back(line);
	}
	return index;
}

std::wstring_view describe(LineError error)
{
	switch (error)
	{
	case LineError::NoExtension: return L"no .lzma, or .bulk, marker";
	case LineError::HashNotWhereExpected: return L"hash is not 32 chars before the extension";
	case LineError::BadHash: return L"hash is not hex";
	case LineError::BadSize: return L"size is not a decimal integer";
	case LineError::UnsafePath: return L"install path escapes the root";
	}
	return L"unknown";
}

std::wstring_view describe(Category category)
{
	switch (category)
	{
	case Category::Cef: return L"cef";
	case Category::Tools: return L"tools";
	case Category::MainExe: return L"exe";
	case Category::Data: return L"data";
	case Category::CacheOrToc: return L"cache";
	}
	return L"?";
}

}
