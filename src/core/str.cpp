#include "core/str.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cwctype>
#include <format>
#include <ranges>

namespace core
{
namespace
{

std::optional<std::uint8_t> nibble(wchar_t c)
{
	if (c >= L'0' && c <= L'9')
		return static_cast<std::uint8_t>(c - L'0');
	if (c >= L'a' && c <= L'f')
		return static_cast<std::uint8_t>(c - L'a' + 10);
	if (c >= L'A' && c <= L'F')
		return static_cast<std::uint8_t>(c - L'A' + 10);
	return std::nullopt;
}

}

std::string narrow(std::wstring_view text)
{
	if (text.empty())
		return {};
	const int size = ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
	                                       nullptr, 0, nullptr, nullptr);
	if (size <= 0)
		return {};
	std::string out(static_cast<std::size_t>(size), '\0');
	::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size,
	                      nullptr, nullptr);
	return out;
}

std::wstring widen(std::string_view text)
{
	if (text.empty())
		return {};
	const int size = ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
	                                       nullptr, 0);
	if (size <= 0)
		return {};
	std::wstring out(static_cast<std::size_t>(size), L'\0');
	::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
	return out;
}

std::string hex(std::span<const std::uint8_t> bytes)
{
	std::string out;
	out.reserve(bytes.size() * 2);
	for (const std::uint8_t byte : bytes)
		out += std::format("{:02X}", byte);
	return out;
}

std::optional<std::array<std::uint8_t, 16>> parseHash(std::wstring_view text)
{
	if (text.size() != 32)
		return std::nullopt;
	std::array<std::uint8_t, 16> out{};
	for (std::size_t i = 0; i < out.size(); ++i)
	{
		const auto high = nibble(text[i * 2]);
		const auto low = nibble(text[i * 2 + 1]);
		if (!high || !low)
			return std::nullopt;
		out[i] = static_cast<std::uint8_t>((*high << 4) | *low);
	}
	return out;
}

std::wstring lower(std::wstring_view text)
{
	std::wstring out(text);
	std::ranges::transform(out, out.begin(),
	                       [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
	return out;
}

bool equalsNoCase(std::wstring_view a, std::wstring_view b)
{
	if (a.size() != b.size())
		return false;
	if (a.empty())
		return true;
	return ::CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(),
	                              static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}

bool containsNoCase(std::wstring_view haystack, std::wstring_view needle)
{
	if (needle.empty())
		return true;
	if (needle.size() > haystack.size())
		return false;
	const std::size_t last = haystack.size() - needle.size();
	for (std::size_t i = 0; i <= last; ++i)
		if (equalsNoCase(haystack.substr(i, needle.size()), needle))
			return true;
	return false;
}

bool startsWithNoCase(std::wstring_view text, std::wstring_view prefix)
{
	return text.size() >= prefix.size() && equalsNoCase(text.substr(0, prefix.size()), prefix);
}

bool endsWithNoCase(std::wstring_view text, std::wstring_view suffix)
{
	return text.size() >= suffix.size()
	       && equalsNoCase(text.substr(text.size() - suffix.size()), suffix);
}

std::string formatBytes(std::uint64_t bytes)
{
	constexpr std::array<std::string_view, 5> units{"B", "KiB", "MiB", "GiB", "TiB"};
	double value = static_cast<double>(bytes);
	std::size_t unit = 0;
	while (value >= 1024.0 && unit + 1 < units.size())
	{
		value /= 1024.0;
		++unit;
	}
	return unit == 0 ? std::format("{} B", bytes) : std::format("{:.1f} {}", value, units[unit]);
}

}
