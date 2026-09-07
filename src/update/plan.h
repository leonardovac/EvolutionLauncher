#pragma once

#include "update/manifest.h"

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
};

std::wstring_view originHost(Branch branch);
std::wstring_view contentHost(Branch branch);
std::wstring_view branchName(Branch branch);
std::filesystem::path defaultRoot(Branch branch);
std::wstring defaultLanguage();
bool defaultSteam();
bool defaultEos();
bool defaultDx12();

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

Plan buildPlan(std::span<const Entry> entries, const Config& config);

std::wstring_view describe(Reason reason);

}
