#include "app/rate.h"

#include "core/str.h"

#include <cmath>
#include <format>

namespace app
{
namespace
{

constexpr float windowSeconds = 0.5f;
constexpr float smoothing = 0.35f;

}

void RateMeter::sample(std::uint64_t downloaded, float dt)
{
    if (!started_)
    {
        previous_ = downloaded;
        started_ = true;
        return;
    }
    if (downloaded < previous_)
    {
        reset();
        previous_ = downloaded;
        started_ = true;
        return;
    }
    pending_ += static_cast<float>(downloaded - previous_);
    previous_ = downloaded;
    accumulated_ += dt;
    if (accumulated_ < windowSeconds)
        return;
    const float instant = pending_ / accumulated_;
    rate_ = ready_ ? rate_ + (instant - rate_) * smoothing : instant;
    ready_ = true;
    pending_ = 0.f;
    accumulated_ = 0.f;
}

void RateMeter::reset()
{
    previous_ = 0;
    accumulated_ = 0.f;
    pending_ = 0.f;
    rate_ = 0.f;
    started_ = false;
    ready_ = false;
}

std::string formatRate(float bytesPerSecond)
{
    if (bytesPerSecond <= 0.f)
        return {};
    return std::format("{}/s", core::formatBytes(static_cast<std::uint64_t>(bytesPerSecond)));
}

std::string formatEta(std::uint64_t remaining, float bytesPerSecond)
{
    if (bytesPerSecond <= 1.f || remaining == 0)
        return {};
    const auto seconds = static_cast<std::uint64_t>(static_cast<float>(remaining) / bytesPerSecond);
    if (seconds < 60)
        return std::format("~{}s remaining", seconds);
    if (seconds < 3600)
        return std::format("~{} min remaining", (seconds + 59) / 60);
    return std::format("~{} h remaining", (seconds + 3599) / 3600);
}

}
