#include "update/skiplist.h"

#include "core/log.h"
#include "core/str.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>

namespace wf
{
namespace
{

// present in the index but optional at runtime; users routinely delete it on purpose
constexpr std::array<std::wstring_view, 1> defaults{L"Tools\\Windows\\x64\\discord_game_sdk.dll"};

std::wstring normalise(std::wstring_view path)
{
	std::wstring out = core::lower(path);
	std::ranges::replace(out, L'/', L'\\');
	while (!out.empty() && (out.front() == L'\\' || out.front() == L' ' || out.front() == L'\t'))
		out.erase(out.begin());
	while (!out.empty() && (out.back() == L'\r' || out.back() == L' ' || out.back() == L'\t'))
		out.pop_back();
	return out;
}

std::filesystem::path skipFilePath()
{
	wchar_t module[MAX_PATH]{};
	const DWORD length = ::GetModuleFileNameW(nullptr, module, MAX_PATH);
	if (length == 0 || length == MAX_PATH)
		return {};
	return std::filesystem::path(module, module + length).parent_path() / L"skip.txt";
}

}

SkipList SkipList::load()
{
	SkipList list;
	for (const std::wstring_view path : defaults)
		list.paths_.push_back(normalise(path));

	const std::filesystem::path file = skipFilePath();
	if (file.empty())
		return list;
	std::ifstream input(file);
	if (!input)
		return list;

	constexpr std::string_view bom = "\xEF\xBB\xBF";
	std::string line;
	bool firstLine = true;
	while (std::getline(input, line))
	{
		if (firstLine)
		{
			firstLine = false;
			if (line.starts_with(bom))
				line.erase(0, bom.size());
		}
		const std::wstring entry = normalise(core::widen(line));
		if (entry.empty() || entry.front() == L'#')
			continue;
		if (entry.front() == L'-')
		{
			std::erase(list.paths_, entry.substr(1));
			continue;
		}
		if (!std::ranges::contains(list.paths_, entry))
			list.paths_.push_back(entry);
	}
	core::info("skip list: {} paths", list.paths_.size());
	return list;
}

bool SkipList::contains(std::wstring_view installPath) const
{
	return std::ranges::contains(paths_, normalise(installPath));
}

}
