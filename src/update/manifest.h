#pragma once

#include "update/md5.h"

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace wf
{

enum class Compression
{
	Lzma,
	Bulk
};

enum class Category
{
	Cef,
	Tools,
	MainExe,
	Data,
	CacheOrToc
};

struct Entry
{
	std::wstring urlPath;      // request path, extension kept, comma and size dropped
	std::wstring installPath;  // relative to the branch root, backslash separated
	Digest hash{};             // MD5 of the installed bytes, not of the download
	std::uint64_t wireSize = 0;
	Compression compression = Compression::Lzma;
	Category category = Category::Data;
};

enum class LineError
{
	NoExtension,
	HashNotWhereExpected,
	BadHash,
	BadSize,
	UnsafePath
};

struct Index
{
	std::vector<Entry> entries;
	std::vector<std::wstring> rejected;
};

std::expected<Entry, LineError> parseLine(std::wstring_view line);
Index parseIndex(std::wstring_view text);

std::wstring_view describe(LineError error);
std::wstring_view describe(Category category);

}
