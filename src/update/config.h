#pragma once

#include "update/md5.h"

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace wf
{

struct PatchRecord
{
	Digest source{};  // the index-expected hash the patch was applied on top of
	Digest result{};  // the hash of the file after the patch
};

class LauncherConfig
{
public:
	static LauncherConfig load();
	bool save() const;

	bool isExcluded(std::wstring_view installPath) const;
	bool isProtected(std::wstring_view relativePath) const;

	const PatchRecord* patchFor(std::wstring_view installPath) const;
	void recordPatch(std::wstring_view installPath, const Digest& source, const Digest& result);

	std::span<const std::wstring> excludedPaths() const { return exclude_; }

	// launcher-wide, not per-title; empty until the file names it or a save writes one
	std::optional<bool> allowNetworkCaches() const { return allowNetworkCaches_; }
	void setAllowNetworkCaches(bool value) { allowNetworkCaches_ = value; }

	// launcher-wide: which tab the GUI opens on, empty until a save records one
	std::optional<std::wstring> lastTitle() const { return lastTitle_; }
	void setLastTitle(std::wstring_view value) { lastTitle_ = std::wstring(value); }

	// launcher-wide: whether the game exe is patched for sideloading after an update
	std::optional<bool> sideload() const { return sideload_; }
	void setSideload(bool value) { sideload_ = value; }

private:
	std::vector<std::wstring> exclude_;
	std::vector<std::wstring> protect_;
	std::unordered_map<std::wstring, PatchRecord> patched_;
	std::optional<bool> allowNetworkCaches_;
	std::optional<std::wstring> lastTitle_;
	std::optional<bool> sideload_;
};

}
