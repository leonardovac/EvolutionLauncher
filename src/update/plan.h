#pragma once

#include "update/manifest.h"
#include "update/progress.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace wf
{

enum class Branch
{
	Public,
	Test,
	Dev
};

struct Config
{
	std::filesystem::path root;
	std::wstring language = L"en";
	Branch branch = Branch::Public;
	bool steam = false;
	bool eosSdk = false;
	bool dx12 = false;
	bool hashCaches = false;
	bool forceHttps = false;
};

std::wstring_view originHost(Branch branch);
std::wstring_view contentHost(Branch branch, bool forceHttps);
std::wstring_view branchName(Branch branch);

bool appliesToClient(const Entry& entry, const Config& config);

enum class Reason
{
	Missing,
	HashMismatch,
	Sibling
};

struct Job
{
	const Entry* entry = nullptr;
	Reason reason = Reason::Missing;
};

struct Plan
{
	std::vector<Job> jobs;
	std::uint64_t downloadBytes = 0;
	std::size_t filtered = 0;
	std::size_t upToDate = 0;
	std::size_t hashed = 0;
};

Plan buildPlan(std::span<const Entry> entries, const Config& config, Progress* progress = nullptr);

std::wstring_view describe(Reason reason);

}
