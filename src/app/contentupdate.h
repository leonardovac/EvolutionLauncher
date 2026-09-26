#pragma once

#include "app/launch.h"
#include "app/settings.h"
#include "core/cancel.h"
#include "update/plan.h"

#include <cstdint>
#include <expected>
#include <functional>

namespace app
{

struct ContentUpdateResult
{
    std::uint32_t exitCode = 0;
    // the applet logged "All stripped assets preprocessed"; it can exit 0 without it
    bool complete = false;
};

// runs the game's ContentUpdate applet to the end, stopping it gracefully once cancel is requested
std::expected<ContentUpdateResult, LaunchError> runContentUpdate(const Settings& settings, wf::Branch branch, const core::CancelToken& cancel, const std::function<void(std::uint64_t downloaded, std::uint64_t total)>& onProgress);

}
