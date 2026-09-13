#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace wf
{

// index paths the user has opted out of fetching; a skipped file stays missing
class SkipList
{
public:
	static SkipList load();

	[[nodiscard]] bool contains(std::wstring_view installPath) const;
	[[nodiscard]] std::size_t size() const noexcept { return paths_.size(); }

private:
	std::vector<std::wstring> paths_;
};

}
