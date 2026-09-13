#pragma once

#include "update/md5.h"

#include <filesystem>
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

	bool shouldSkip(std::wstring_view installPath) const;
	bool shouldKeep(std::wstring_view relativePath) const;

	const PatchRecord* patchFor(std::wstring_view installPath) const;
	void recordPatch(std::wstring_view installPath, const Digest& source, const Digest& result);

	std::span<const std::wstring> skipPaths() const { return skip_; }

private:
	std::vector<std::wstring> skip_;
	std::vector<std::wstring> keep_;
	std::unordered_map<std::wstring, PatchRecord> patched_;
};

}
