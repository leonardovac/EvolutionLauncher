#include "update/config.h"

#include "core/log.h"
#include "core/str.h"

#include <format>
#include <windows.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <ranges>

namespace wf
{
namespace
{

// already normalised; excluded on every run and never written to launcher.json
constexpr std::array<std::wstring_view, 1> alwaysExcluded{
	L"tools\\windows\\x64\\discord_game_sdk.dll"};

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

// `*` spans separators and `?` takes one character; everything else is literal
bool globMatch(std::wstring_view pattern, std::wstring_view text)
{
	std::size_t p = 0;
	std::size_t t = 0;
	std::size_t star = std::wstring_view::npos;
	std::size_t mark = 0;
	while (t < text.size())
	{
		if (p < pattern.size() && (pattern[p] == L'?' || pattern[p] == text[t]))
		{
			++p;
			++t;
		}
		else if (p < pattern.size() && pattern[p] == L'*')
		{
			star = p++;
			mark = t;
		}
		else if (star != std::wstring_view::npos)
		{
			p = star + 1;
			t = ++mark;
		}
		else
		{
			return false;
		}
	}
	while (p < pattern.size() && pattern[p] == L'*')
		++p;
	return p == pattern.size();
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
	std::optional<bool> boolean()
	{
		skipWs();
		if (text.compare(pos, 4, "true") == 0)
		{
			pos += 4;
			return true;
		}
		if (text.compare(pos, 5, "false") == 0)
		{
			pos += 5;
			return false;
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

bool LauncherConfig::isAlwaysExcluded(std::wstring_view installPath)
{
	return std::ranges::contains(alwaysExcluded, normalise(installPath));
}

bool LauncherConfig::isProtected(std::wstring_view relativePath) const
{
	const std::wstring key = normalise(relativePath);
	const std::size_t slash = key.find_last_of(L'\\');
	const std::wstring_view leaf =
		slash == std::wstring::npos ? std::wstring_view(key) : std::wstring_view(key).substr(slash + 1);
	return std::ranges::any_of(protect_, [&](const std::wstring& pattern) {
		// a pattern with no separator matches the file name wherever it sits
		return globMatch(pattern, pattern.contains(L'\\') ? std::wstring_view(key) : leaf);
	});
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

std::optional<bool> LauncherConfig::sideload(std::wstring_view title) const
{
	const auto found = sideload_.find(core::lower(title));
	return found == sideload_.end() ? std::nullopt : std::optional<bool>(found->second);
}

void LauncherConfig::setSideload(std::wstring_view title, bool value)
{
	sideload_[core::lower(title)] = value;
}

std::optional<std::wstring> LauncherConfig::root(std::wstring_view title) const
{
	const auto found = root_.find(core::lower(title));
	return found == root_.end() ? std::nullopt : std::optional<std::wstring>(found->second);
}

void LauncherConfig::setRoot(std::wstring_view title, std::wstring_view value)
{
	// a path the user reads back, so it keeps its case; only the title key is folded
	root_[core::lower(title)] = std::wstring(value);
}

LauncherConfig LauncherConfig::load(const RunContext& ctx)
{
	LauncherConfig config;
	config.exclude_.assign(alwaysExcluded.begin(), alwaysExcluded.end());

	const std::filesystem::path file = beside(L"launcher.json");
	std::string bytes;
	if (!file.empty())
	{
		std::ifstream input(file, std::ios::binary);
		if (input)
			bytes.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
	}

	if (bytes.empty())
	{
		ctx.log(core::Level::Info, std::format(L"config: {} exclude, {} protect, {} patched",
		                               config.exclude_.size(), config.protect_.size(),
		                               config.patched_.size()));
		return config;
	}

	Scanner scan{bytes};
	if (!scan.consume('{'))
	{
		ctx.log(core::Level::Warn, L"launcher.json: expected an object; using defaults");
		return config;
	}
	while (!scan.peek('}'))
	{
		const auto keyOpt = scan.string();
		if (!keyOpt || !scan.consume(':'))
			break;
		const std::string key = *keyOpt;
		const bool excludeKey = key == "exclude";
		const bool protectKey = key == "protect";
		if (excludeKey || protectKey)
		{
			auto& list = excludeKey ? config.exclude_ : config.protect_;
			list.clear();
			if (excludeKey)
				list.assign(alwaysExcluded.begin(), alwaysExcluded.end());
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
				ctx.log(core::Level::Warn, std::format(L"launcher.json: \"{}\" is not an array; ignoring",
			                               core::widen(key)));
				scan.skipValue();
			}
		}
		else if (key == "allowNetworkCaches")
		{
			if (const auto value = scan.boolean())
				config.allowNetworkCaches_ = *value;
			else
			{
				ctx.log(core::Level::Warn,
			        L"launcher.json: \"allowNetworkCaches\" is not a boolean; ignoring");
				scan.skipValue();
			}
		}
		else if (key == "lastTitle")
		{
			if (const auto value = scan.string())
				config.lastTitle_ = core::widen(*value);
			else
			{
				ctx.log(core::Level::Warn, L"launcher.json: \"lastTitle\" is not a string; ignoring");
				scan.skipValue();
			}
		}
		else if (key == "sideload")
		{
			if (scan.consume('{'))
			{
				while (!scan.peek('}'))
				{
					const auto name = scan.string();
					if (!name || !scan.consume(':'))
						break;
					if (const auto value = scan.boolean())
						config.sideload_[core::lower(core::widen(*name))] = *value;
					else
						scan.skipValue();
					if (!scan.consume(','))
						break;
				}
				scan.consume('}');
			}
			else
			{
				ctx.log(core::Level::Warn, L"launcher.json: \"sideload\" is not an object; ignoring");
				scan.skipValue();
			}
		}
		else if (key == "root")
		{
			if (scan.consume('{'))
			{
				while (!scan.peek('}'))
				{
					const auto name = scan.string();
					if (!name || !scan.consume(':'))
						break;
					if (const auto value = scan.string())
						config.root_[core::lower(core::widen(*name))] = core::widen(*value);
					else
						scan.skipValue();
					if (!scan.consume(','))
						break;
				}
				scan.consume('}');
			}
			else
			{
				ctx.log(core::Level::Warn, L"launcher.json: \"root\" is not an object; ignoring");
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
						ctx.log(core::Level::Warn,
				        std::format(L"launcher.json: patched entry for \"{}\" missing source or "
				                    L"result",
				                    core::widen(*pathOpt)));
					if (!scan.consume(','))
						break;
				}
				scan.consume('}');
			}
			else
			{
				ctx.log(core::Level::Warn, L"launcher.json: \"patched\" is not an object; ignoring");
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

	ctx.log(core::Level::Info, std::format(L"config: {} exclude, {} protect, {} patched",
	                               config.exclude_.size(), config.protect_.size(),
	                               config.patched_.size()));
	return config;
}

namespace
{

struct StoredScalars
{
	std::optional<bool> allowNetworkCaches;
	std::optional<std::wstring> lastTitle;
	std::map<std::wstring, bool> sideload;
	std::map<std::wstring, std::wstring> root;
};

// the worker saves this file to record a patch and owns none of these, so a save that carries
// no opinion must not erase what is on disk
StoredScalars storedScalars()
{
	StoredScalars out;
	const std::filesystem::path file = beside(L"launcher.json");
	if (file.empty())
		return out;
	std::ifstream input(file, std::ios::binary);
	if (!input)
		return out;
	const std::string bytes((std::istreambuf_iterator<char>(input)),
	                        std::istreambuf_iterator<char>());
	Scanner scan{bytes};
	if (!scan.consume('{'))
		return out;
	while (!scan.peek('}'))
	{
		const auto key = scan.string();
		if (!key || !scan.consume(':'))
			break;
		if (*key == "allowNetworkCaches")
			out.allowNetworkCaches = scan.boolean();
		else if (*key == "lastTitle")
		{
			if (const auto value = scan.string())
				out.lastTitle = core::widen(*value);
		}
		else if (*key == "sideload" && scan.consume('{'))
		{
			while (!scan.peek('}'))
			{
				const auto name = scan.string();
				if (!name || !scan.consume(':'))
					break;
				if (const auto value = scan.boolean())
					out.sideload[core::lower(core::widen(*name))] = *value;
				else
					scan.skipValue();
				if (!scan.consume(','))
					break;
			}
			scan.consume('}');
		}
		else if (*key == "root" && scan.consume('{'))
		{
			while (!scan.peek('}'))
			{
				const auto name = scan.string();
				if (!name || !scan.consume(':'))
					break;
				if (const auto value = scan.string())
					out.root[core::lower(core::widen(*name))] = core::widen(*value);
				else
					scan.skipValue();
				if (!scan.consume(','))
					break;
			}
			scan.consume('}');
		}
		else
			scan.skipValue();
		if (!scan.consume(','))
			break;
	}
	return out;
}

}

bool LauncherConfig::save() const
{
	const std::filesystem::path file = beside(L"launcher.json");
	if (file.empty())
		return false;
	// read before the stream truncates, or the values we mean to preserve are already gone
	const StoredScalars stored = storedScalars();
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
	emitList("exclude", exclude_ | std::views::filter([](const std::wstring& path) { return !std::ranges::contains(alwaysExcluded, path); }) | std::ranges::to<std::vector>());
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

	const std::optional<bool> caches =
		allowNetworkCaches_ ? allowNetworkCaches_ : stored.allowNetworkCaches;
	const std::optional<std::wstring> title = lastTitle_ ? lastTitle_ : stored.lastTitle;
	// a save that names no title keeps whatever the file already said about the others
	std::map<std::wstring, bool> patchExe = stored.sideload;
	for (const auto& [name, on] : sideload_)
		patchExe[name] = on;
	std::map<std::wstring, std::wstring> roots = stored.root;
	for (const auto& [name, path] : root_)
		roots[name] = path;
	if (caches)
		out << ",\n  \"allowNetworkCaches\": " << (*caches ? "true" : "false");
	if (title)
		out << ",\n  \"lastTitle\": \"" << escapeJson(core::narrow(*title)) << "\"";
	if (!patchExe.empty())
	{
		out << ",\n  \"sideload\": {";
		std::size_t written = 0;
		for (const auto& [name, on] : patchExe)
		{
			out << (written == 0 ? "\n    \"" : ",\n    \"") << escapeJson(core::narrow(name))
			    << "\": " << (on ? "true" : "false");
			++written;
		}
		out << "\n  }";
	}
	if (!roots.empty())
	{
		out << ",\n  \"root\": {";
		std::size_t written = 0;
		for (const auto& [name, path] : roots)
		{
			out << (written == 0 ? "\n    \"" : ",\n    \"") << escapeJson(core::narrow(name))
			    << "\": \"" << escapeJson(core::narrow(path)) << "\"";
			++written;
		}
		out << "\n  }";
	}
	out << "\n}\n";
	return static_cast<bool>(out);
}

}
