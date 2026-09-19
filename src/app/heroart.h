#pragma once

#include "update/plan.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <thread>
#include <vector>

namespace app
{

// Key art for the current update, scraped from the title's patch notes page; the baked-in hero
// is the fallback and is what draws until this lands.
class HeroArt
{
public:
    ~HeroArt();

    // reads the cached art on the calling thread, then starts the scrape; the bytes are frame one
    [[nodiscard]] std::vector<std::uint8_t> begin(wf::Title title);
    void stop();
    // the encoded image, handed over once; empty until the fetch lands
    [[nodiscard]] std::vector<std::uint8_t> take();

private:
    std::vector<std::uint8_t> warm(wf::Title title);
    void work();

    std::thread thread_;
    wf::Title title_ = wf::Title::Warframe;
    // written by begin(), read by the worker; the thread start orders the two
    std::filesystem::path warmFile_;
    std::mutex mutex_;
    std::vector<std::uint8_t> bytes_;
    std::atomic<bool> stop_{false};
};

}
