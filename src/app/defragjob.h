#pragma once

#include "app/launch.h"
#include "app/settings.h"
#include "update/plan.h"

#include <atomic>
#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <thread>

namespace app
{

using DefragText = std::shared_ptr<const std::string>;

struct DefragSnapshot
{
    bool running = false;
    bool finished = false;
    std::uint64_t processed = 0;
    std::uint64_t total = 0;
    DefragText currentFile;
    std::uint32_t exitCode = 0;
};

// runs the cache defragmenter with its console hidden and reads progress from its title
class DefragJob
{
public:
    ~DefragJob();

    std::expected<void, LaunchError> start(const Settings& settings, wf::Branch branch);
    void join();
    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] DefragSnapshot snapshot() const;
    void clearFinished() noexcept;

private:
    void pump(void* process);

    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> finished_{false};
    std::atomic<std::uint64_t> processed_{0};
    std::atomic<std::uint64_t> total_{0};
    std::atomic<std::uint32_t> exitCode_{0};
    std::atomic<DefragText> currentFile_;
};

}
