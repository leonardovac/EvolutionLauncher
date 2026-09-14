#include "app/heroart.h"

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

constexpr std::wstring_view pageUrl = L"https://www.warframe.com/en/patch-notes";
constexpr std::string_view recordMark = "class=\"record\"";
constexpr std::string_view assetPrefix = "https://www-static.warframe.com/uploads/";
constexpr std::size_t pageCap = 4u << 20;
constexpr std::size_t imageCap = 24u << 20;
constexpr DWORD timeoutMs = 8000;

std::filesystem::path cacheDir()
{
    wchar_t module[MAX_PATH]{};
    const DWORD length = ::GetModuleFileNameW(nullptr, module, MAX_PATH);
    if (length == 0 || length == MAX_PATH)
        return {};
    return std::filesystem::path(module, module + length).parent_path() / L"cache";
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

// the first record card on the page is the newest update
std::optional<std::string> artUrl(std::string_view page)
{
    const std::size_t record = page.find(recordMark);
    if (record == std::string_view::npos)
        return std::nullopt;
    const std::size_t start = page.find(assetPrefix, record);
    if (start == std::string_view::npos)
        return std::nullopt;
    const std::size_t end = page.find('"', start);
    if (end == std::string_view::npos)
        return std::nullopt;
    std::string url(page.substr(start, end - start));
    if (!nameSafe(std::string_view(url).substr(assetPrefix.size())))
        return std::nullopt;
    return url;
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

// only ever one piece of art here, so the previous update's is dropped once the new one lands
void pruneCache(const std::filesystem::path& dir, const std::filesystem::path& keep)
{
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
    {
        if (!entry.is_regular_file(ec) || entry.path().filename() == keep.filename())
            continue;
        if (nameSafe(entry.path().filename().string()))
            std::filesystem::remove(entry.path(), ec);
    }
}

std::vector<std::uint8_t> cached(const std::filesystem::path& dir)
{
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
    {
        if (entry.is_regular_file(ec) && nameSafe(entry.path().filename().string()))
            return readFile(entry.path());
    }
    return {};
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

void HeroArt::start()
{
    if (!thread_.joinable())
        thread_ = std::thread(&HeroArt::work, this);
}

void HeroArt::stop()
{
    stop_.store(true, std::memory_order_relaxed);
    if (thread_.joinable())
        thread_.join();
}

std::vector<std::uint8_t> HeroArt::take()
{
    const std::lock_guard lock(mutex_);
    return std::move(bytes_);
}

void HeroArt::work()
{
    const std::filesystem::path dir = cacheDir();
    if (dir.empty())
        return;

    const auto publish = [this](std::vector<std::uint8_t> bytes) {
        if (bytes.empty())
            return;
        const std::lock_guard lock(mutex_);
        bytes_ = std::move(bytes);
    };

    const auto session = wf::Session::open();
    if (!session)
    {
        publish(cached(dir));
        return;
    }
    ::WinHttpSetTimeouts(session->handle(), timeoutMs, timeoutMs, timeoutMs, timeoutMs);

    const auto page = get(*session, pageUrl, pageCap, stop_);
    const auto url = page ? artUrl(std::string_view(reinterpret_cast<const char*>(page->data()),
                                                    page->size()))
                          : std::nullopt;
    if (!url)
    {
        core::warn("hero art: the patch notes page named none, using the cache");
        publish(cached(dir));
        return;
    }

    const std::filesystem::path file = dir / core::widen(std::string_view(*url).substr(assetPrefix.size()));
    if (std::vector<std::uint8_t> hit = readFile(file); !hit.empty())
    {
        publish(std::move(hit));
        return;
    }

    auto image = get(*session, core::widen(*url), imageCap, stop_);
    if (!image || image->empty())
    {
        publish(cached(dir));
        return;
    }

    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    writeFile(file, *image);
    // a write that failed leaves the previous art in place rather than emptying the cache
    if (std::filesystem::exists(file, ec))
        pruneCache(dir, file);
    core::info("hero art: fetched {}", *url);
    publish(std::move(*image));
}

}
