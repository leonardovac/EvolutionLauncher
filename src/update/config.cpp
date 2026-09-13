#include "update/config.h"

#include "core/log.h"
#include "core/str.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <fstream>

namespace wf
{
namespace
{

constexpr std::array<std::wstring_view, 1> excludeDefaults{
	L"Tools\\Windows\\x64\\discord_game_sdk.dll"};

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

std::filesystem::path beside(std::wstring_view name)
{
	wchar_t module[MAX_PATH]{};
	const DWORD length = ::GetModuleFileNameW(nullptr, module, MAX_PATH);
	if (length == 0 || length == MAX_PATH)
		return {};
	return std::filesystem::path(module, module + length).parent_path() / name;
}

std::string escapeJson(std::string_view text)
{
	std::string out;
	out.reserve(text.size());
	for (const char c : text)
	{
		if (c == '\\' || c == '"')
			out.push_back('\\');
		out.push_back(c);
	}
	return out;
}

// minimal JSON scanner over a UTF-8 byte string; enough for the closed schema below
struct Scanner
{
	std::string_view text;
	std::size_t pos = 0;

	void skipWs()
	{
		while (pos < text.size() &&
		       (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\r' || text[pos] == '\n'))
			++pos;
	}
	bool consume(char c)
	{
		skipWs();
		if (pos < text.size() && text[pos] == c)
		{
			++pos;
			return true;
		}
		return false;
	}
	bool peek(char c)
	{
		skipWs();
		return pos < text.size() && text[pos] == c;
	}
	std::optional<std::string> string()
	{
		skipWs();
		if (pos >= text.size() || text[pos] != '"')
			return std::nullopt;
		++pos;
		std::string out;
		while (pos < text.size())
		{
			const char c = text[pos++];
			if (c == '"')
				return out;
			if (c == '\\' && pos < text.size())
			{
				// every string here is a Windows path, so `\n` is a separator, not a newline
				const char e = text[pos++];
				constexpr std::array<char, 3> unescaped{'"', '\\', '/'};
				if (!std::ranges::contains(unescaped, e))
					out.push_back('\\');
				out.push_back(e);
				continue;
			}
			out.push_back(c);
		}
		return std::nullopt;
	}
	// skips one value we do not care about: object, array, string, literal or number
	void skipValue()
	{
		skipWs();
		if (pos >= text.size())
			return;
		const char c = text[pos];
		if (c == '"')
		{
			string();
			return;
		}
		if (c == '{' || c == '[')
		{
			const char close = c == '{' ? '}' : ']';
			++pos;
			int depth = 1;
			while (pos < text.size() && depth > 0)
			{
				const char d = text[pos];
				if (d == '"')
				{
					string();
					continue;
				}
				if (d == c)
					++depth;
				else if (d == close)
					--depth;
				++pos;
			}
			return;
		}
		while (pos < text.size() && text[pos] != ',' && text[pos] != '}' && text[pos] != ']')
			++pos;
	}
};

}

bool LauncherConfig::isExcluded(std::wstring_view installPath) const
{
	return std::ranges::contains(exclude_, normalise(installPath));
}

bool LauncherConfig::isProtected(std::wstring_view relativePath) const
{
	return std::ranges::contains(protect_, normalise(relativePath));
}

const PatchRecord* LauncherConfig::patchFor(std::wstring_view installPath) const
{
	const auto found = patched_.find(normalise(installPath));
	return found == patched_.end() ? nullptr : &found->second;
}

void LauncherConfig::recordPatch(std::wstring_view installPath, const Digest& source,
                                 const Digest& result)
{
	patched_[normalise(installPath)] = PatchRecord{source, result};
}

LauncherConfig LauncherConfig::load()
{
	LauncherConfig config;
	for (const std::wstring_view path : excludeDefaults)
		config.exclude_.push_back(normalise(path));

	const std::filesystem::path file = beside(L"launcher.json");
	std::string bytes;
	if (!file.empty())
	{
		std::ifstream input(file, std::ios::binary);
		if (input)
			bytes.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
	}

	// one-time migration: no launcher.json yet, but a legacy skip.txt beside us
	if (bytes.empty())
	{
		const std::filesystem::path legacy = beside(L"skip.txt");
		std::ifstream input(legacy, std::ios::binary);
		if (input)
		{
			constexpr std::string_view bom = "\xEF\xBB\xBF";
			std::string line;
			bool first = true;
			while (std::getline(input, line))
			{
				if (first && line.starts_with(bom))
					line.erase(0, bom.size());
				first = false;
				const std::wstring entry = normalise(core::widen(line));
				if (entry.empty() || entry.front() == L'#')
					continue;
				if (entry.front() == L'-')
				{
					std::erase(config.exclude_, entry.substr(1));
					continue;
				}
				if (!std::ranges::contains(config.exclude_, entry))
					config.exclude_.push_back(entry);
			}
			config.save();
		}
		core::info("config: {} exclude, {} protect, {} patched", config.exclude_.size(),
		           config.protect_.size(), config.patched_.size());
		return config;
	}

	Scanner scan{bytes};
	if (!scan.consume('{'))
	{
		core::warn("launcher.json: expected an object; using defaults");
		return config;
	}
	while (!scan.peek('}'))
	{
		const auto keyOpt = scan.string();
		if (!keyOpt || !scan.consume(':'))
			break;
		const std::string key = *keyOpt;
		// skip/keep were the original spellings; read them so an early file still loads
		const bool excludeKey = key == "exclude" || key == "skip";
		const bool protectKey = key == "protect" || key == "keep";
		if (excludeKey || protectKey)
		{
			auto& list = excludeKey ? config.exclude_ : config.protect_;
			list.clear();
			if (excludeKey)
				for (const std::wstring_view path : excludeDefaults)
					list.push_back(normalise(path));
			if (scan.consume('['))
			{
				while (!scan.peek(']'))
				{
					const auto item = scan.string();
					if (!item)
						break;
					const std::wstring entry = normalise(core::widen(*item));
					if (!entry.empty() && !std::ranges::contains(list, entry))
						list.push_back(entry);
					if (!scan.consume(','))
						break;
				}
				scan.consume(']');
			}
			else
			{
				core::warn("launcher.json: \"{}\" is not an array; ignoring", key);
				scan.skipValue();
			}
		}
		else if (key == "patched")
		{
			if (scan.consume('{'))
			{
				while (!scan.peek('}'))
				{
					const auto pathOpt = scan.string();
					if (!pathOpt || !scan.consume(':') || !scan.consume('{'))
						break;
					Digest source{};
					Digest result{};
					bool haveSource = false;
					bool haveResult = false;
					while (!scan.peek('}'))
					{
						const auto field = scan.string();
						if (!field || !scan.consume(':'))
							break;
						const auto value = scan.string();
						if (!value)
							break;
						if (const auto d = parseDigest(*value))
						{
							if (*field == "source")
							{
								source = *d;
								haveSource = true;
							}
							else if (*field == "result")
							{
								result = *d;
								haveResult = true;
							}
						}
						if (!scan.consume(','))
							break;
					}
					scan.consume('}');
					if (haveSource && haveResult)
						config.patched_[normalise(core::widen(*pathOpt))] =
							PatchRecord{source, result};
					else
						core::warn("launcher.json: patched entry for \"{}\" missing source or result", *pathOpt);
					if (!scan.consume(','))
						break;
				}
				scan.consume('}');
			}
			else
			{
				core::warn("launcher.json: \"patched\" is not an object; ignoring");
				scan.skipValue();
			}
		}
		else
		{
			scan.skipValue();
		}
		if (!scan.consume(','))
			break;
	}

	core::info("config: {} exclude, {} protect, {} patched", config.exclude_.size(),
	           config.protect_.size(), config.patched_.size());
	return config;
}

bool LauncherConfig::save() const
{
	const std::filesystem::path file = beside(L"launcher.json");
	if (file.empty())
		return false;
	std::ofstream out(file, std::ios::binary | std::ios::trunc);
	if (!out)
		return false;

	auto emitList = [&out](std::string_view name, std::span<const std::wstring> items) {
		out << "  \"" << name << "\": [";
		for (std::size_t i = 0; i < items.size(); ++i)
			out << (i == 0 ? "\n    \"" : ",\n    \"") << escapeJson(core::narrow(items[i]))
			    << "\"";
		out << (items.empty() ? "]" : "\n  ]");
	};

	out << "{\n";
	emitList("exclude", exclude_);
	out << ",\n";
	emitList("protect", protect_);
	out << ",\n  \"patched\": {";
	std::size_t i = 0;
	for (const auto& [path, record] : patched_)
	{
		out << (i == 0 ? "\n    \"" : ",\n    \"") << escapeJson(core::narrow(path))
		    << "\": { \"source\": \"" << toHex(record.source) << "\", \"result\": \""
		    << toHex(record.result) << "\" }";
		++i;
	}
	out << (patched_.empty() ? " }" : "\n  }");
	out << "\n}\n";
	return static_cast<bool>(out);
}

}
