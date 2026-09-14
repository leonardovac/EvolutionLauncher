#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

namespace app
{

// Key art for the current update, scraped from the patch notes page; the baked-in hero is the
// fallback and is what draws until this lands.
class HeroArt
{
public:
    ~HeroArt();

    void start();
    void stop();
    // the encoded image, handed over once; empty until the fetch lands
    [[nodiscard]] std::vector<std::uint8_t> take();

private:
    void work();

    std::thread thread_;
    std::mutex mutex_;
    std::vector<std::uint8_t> bytes_;
    std::atomic<bool> stop_{false};
};

}
