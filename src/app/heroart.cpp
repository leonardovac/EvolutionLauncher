#include "app/heroart.h"

#include "app/titles.h"
#include "core/log.h"
#include "core/str.h"
#include "update/http.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <span>
#include <system_error>

namespace app
{
namespace
{

constexpr std::string_view recordMark = "class=\"record\"";
constexpr std::size_t pageCap = 4u << 20;
constexpr std::size_t imageCap = 24u << 20;
constexpr DWORD timeoutMs = 8000;

struct Scrape
{
    std::wstring_view page;
    std::string_view assetPrefix;
    // Soulframe lists a webp thumbnail, so the full-size original has to be derived from it
    bool fromConversion;
};

constexpr std::array<Scrape, 2> scrapes{
    {{L"https://www.warframe.com/en/patch-notes", "https://www-static.warframe.com/uploads/",
      false},
     {L"https://www.soulframe.com/en/patch-notes",
      "https://sfweb.nyc3.cdn.digitaloceanspaces.com/", true}}};

// scrapes is indexed by the enum, matching profiles in titles.cpp
const Scrape& scrape(wf::Title title)
{
    const auto index = static_cast<std::size_t>(title);
    return scrapes[index < scrapes.size() ? index : 0];
}

std::filesystem::path cacheDir()
{
    wchar_t module[MAX_PATH]{};
    const DWORD length = ::GetModuleFileNameW(nullptr, module, MAX_PATH);
    if (length == 0 || length == MAX_PATH)
        return {};
    return std::filesystem::path(module, module + length).parent_path() / L"cache";
}

// one flat folder, so the title tags the file and a prune can tell whose art it is holding
std::wstring cachePrefix(wf::Title title)
{
    return std::wstring(profile(title).localFolder) + L'-';
}

// the file name comes off a web page, so it is never let near a path until it is plainly inert
bool nameSafe(std::string_view name)
{
    constexpr std::array<std::string_view, 2> allowed{".png", ".jpg"};
    if (name.empty() || name.size() > 80)
        return false;
    if (!std::ranges::any_of(allowed, [name](std::string_view ext) { return name.ends_with(ext); }))
        return false;
    return std::ranges::all_of(name, [](unsigned char c) {
        return std::isalnum(c) != 0 || c == '.' || c == '-' || c == '_';
    });
}

// conversions/<name>-preview.webp is a thumbnail; the original sits one level up as <name>.png
std::optional<std::string> original(std::string_view url)
{
    constexpr std::string_view folder = "conversions/";
    constexpr std::string_view suffix = "-preview.webp";
    const std::size_t at = url.rfind(folder);
    if (at == std::string_view::npos || !url.ends_with(suffix))
        return std::nullopt;
    const std::string_view stem =
        url.substr(at + folder.size(), url.size() - at - folder.size() - suffix.size());
    return std::string(url.substr(0, at)).append(stem).append(".png");
}

// the art URL off the page, and the basename it caches under
struct Art
{
    std::string url;
    std::string name;
};

// the first record card on the page is the newest update
std::optional<Art> artUrl(std::string_view page, const Scrape& how)
{
    const std::size_t record = page.find(recordMark);
    if (record == std::string_view::npos)
        return std::nullopt;
    const std::size_t start = page.find(how.assetPrefix, record);
    if (start == std::string_view::npos)
        return std::nullopt;
    const std::size_t end = page.find('"', start);
    if (end == std::string_view::npos)
        return std::nullopt;

    std::string url(page.substr(start, end - start));
    if (how.fromConversion)
    {
        const auto full = original(url);
        if (!full)
            return std::nullopt;
        url = *full;
    }
    // only the basename ever reaches the disk, and it is checked before it does
    const std::size_t slash = url.rfind('/');
    std::string name = slash == std::string::npos ? url : url.substr(slash + 1);
    if (!nameSafe(name))
        return std::nullopt;
    return Art{std::move(url), std::move(name)};
}

std::vector<std::uint8_t> readFile(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return {};
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(file),
                                     std::istreambuf_iterator<char>());
}

void writeFile(const std::filesystem::path& path, std::span<const std::uint8_t> bytes)
{
    const std::filesystem::path temp = std::filesystem::path(path).concat(L".tmp");
    {
        std::ofstream file(temp, std::ios::binary | std::ios::trunc);
        if (!file)
            return;
        file.write(reinterpret_cast<const char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
        if (!file)
            return;
    }
    std::error_code ec;
    std::filesystem::rename(temp, path, ec);
    if (ec)
        std::filesystem::remove(temp, ec);
}

// only ever one piece of art per title, so the previous update's is dropped once the new one lands
void pruneCache(const std::filesystem::path& dir, std::wstring_view prefix,
                const std::filesystem::path& keep)
{
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
    {
        if (!entry.is_regular_file(ec) || entry.path().filename() == keep.filename())
            continue;
        const std::wstring name = entry.path().filename().wstring();
        if (name.starts_with(prefix) && nameSafe(entry.path().filename().string()))
            std::filesystem::remove(entry.path(), ec);
    }
}

std::filesystem::path cached(const std::filesystem::path& dir, std::wstring_view prefix)
{
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
    {
        const std::wstring name = entry.path().filename().wstring();
        if (entry.is_regular_file(ec) && name.starts_with(prefix)
            && nameSafe(entry.path().filename().string()))
            return entry.path();
    }
    return {};
}

// art from before the cache tagged files by title; it was Warframe's, since only it had any
void pruneUntagged(const std::filesystem::path& dir)
{
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
    {
        if (!entry.is_regular_file(ec) || !nameSafe(entry.path().filename().string()))
            continue;
        const std::wstring name = entry.path().filename().wstring();
        const bool tagged = std::ranges::any_of(titleProfiles(), [&name](const TitleProfile& it) {
            return name.starts_with(std::wstring(it.localFolder) + L'-');
        });
        if (!tagged)
            std::filesystem::remove(entry.path(), ec);
    }
}

std::optional<std::vector<std::uint8_t>> get(const wf::Session& session, std::wstring_view url,
                                             std::size_t cap, const std::atomic<bool>& stop)
{
    const auto connection = wf::Connection::open(session, url);
    if (!connection)
        return std::nullopt;

    std::vector<std::uint8_t> body;
    const auto sink = [&body, cap, &stop](std::span<const std::uint8_t> chunk) {
        if (stop.load(std::memory_order_relaxed) || body.size() + chunk.size() > cap)
            return false;
        body.insert(body.end(), chunk.begin(), chunk.end());
        return true;
    };
    if (!connection->fetch({}, 0, sink))
        return std::nullopt;
    return body;
}

}

HeroArt::~HeroArt()
{
    stop();
}

std::vector<std::uint8_t> HeroArt::begin(wf::Title title)
{
    title_ = title;
    std::vector<std::uint8_t> bytes = warm(title);
    if (!thread_.joinable())
        thread_ = std::thread(&HeroArt::work, this);
    return bytes;
}

std::vector<std::uint8_t> HeroArt::warm(wf::Title title)
{
    const std::filesystem::path dir = cacheDir();
    if (dir.empty())
        return {};
    pruneUntagged(dir);
    const std::filesystem::path file = cached(dir, cachePrefix(title));
    if (file.empty())
        return {};
    std::vector<std::uint8_t> bytes = readFile(file);
    // an unreadable file leaves this empty, which sends the worker down the re-fetch path
    if (!bytes.empty())
        warmFile_ = file;
    return bytes;
}

void HeroArt::stop()
{
    stop_.store(true, std::memory_order_relaxed);
    if (thread_.joinable())
        thread_.join();
    stop_.store(false, std::memory_order_relaxed);
    warmFile_.clear();
    title_ = wf::Title::Warframe;
    // art that landed belongs to the title that asked for it, so a stop drops it unseen
    const std::lock_guard lock(mutex_);
    bytes_.clear();
}

std::vector<std::uint8_t> HeroArt::take()
{
    const std::lock_guard lock(mutex_);
    return std::move(bytes_);
}

void HeroArt::work()
{
    const Scrape& how = scrape(title_);
    const std::filesystem::path dir = cacheDir();
    if (dir.empty())
        return;
    const std::wstring prefix = cachePrefix(title_);

    const auto publish = [this](std::vector<std::uint8_t> bytes) {
        if (bytes.empty())
            return;
        const std::lock_guard lock(mutex_);
        bytes_ = std::move(bytes);
    };

    const auto session = wf::Session::open();
    if (!session)
        return;
    ::WinHttpSetTimeouts(session->handle(), timeoutMs, timeoutMs, timeoutMs, timeoutMs);

    const auto page = get(*session, how.page, pageCap, stop_);
    const auto art = page ? artUrl(std::string_view(reinterpret_cast<const char*>(page->data()),
                                                    page->size()),
                                   how)
                          : std::nullopt;
    if (!art)
    {
        core::warn("hero art: the patch notes page named none, keeping the cache");
        return;
    }

    const std::filesystem::path file = dir / (prefix + core::widen(art->name));
    // warm() already drew this one, so there is nothing newer to hand over
    if (!warmFile_.empty() && file == warmFile_)
        return;
    if (std::vector<std::uint8_t> hit = readFile(file); !hit.empty())
    {
        publish(std::move(hit));
        return;
    }

    auto image = get(*session, core::widen(art->url), imageCap, stop_);
    if (!image || image->empty())
        return;

    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    writeFile(file, *image);
    // a write that failed leaves the previous art in place rather than emptying the cache
    if (std::filesystem::exists(file, ec))
        pruneCache(dir, prefix, file);
    core::info("hero art: fetched {}", art->url);
    publish(std::move(*image));
}

}
