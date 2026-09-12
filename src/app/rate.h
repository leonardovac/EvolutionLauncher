#pragma once

#include <cstdint>
#include <string>

namespace app
{

// exponential average over a byte counter; the worker stays a plain counter
class RateMeter
{
public:
    void sample(std::uint64_t downloaded, float dt);
    void reset();

    [[nodiscard]] float bytesPerSecond() const noexcept { return rate_; }
    [[nodiscard]] bool ready() const noexcept { return ready_; }

private:
    std::uint64_t previous_ = 0;
    float accumulated_ = 0.f;
    float pending_ = 0.f;
    float rate_ = 0.f;
    bool started_ = false;
    bool ready_ = false;
};

std::string formatRate(float bytesPerSecond);

// empty when the rate is not usable yet
std::string formatEta(std::uint64_t remaining, float bytesPerSecond);

}
