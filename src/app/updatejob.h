#pragma once

#include "app/jobphase.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

namespace app
{

// shared so the UI can hold a frame's text without copying it off the worker
using JobText = std::shared_ptr<const std::string>;

struct JobSnapshot
{
    JobPhase phase = JobPhase::Idle;
    std::size_t entryIndex = 0;
    std::size_t entryCount = 0;
    std::uint64_t downloaded = 0;
    std::uint64_t downloadTotal = 0;
    std::uint64_t hashedBytes = 0;
    JobText currentFile;
    JobText message;
};

class UpdateJob
{
public:
    ~UpdateJob();

    void start();
    void cancel();
    void join();
    void restart(bool verify = false);
    void startStaleReport();
    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] JobSnapshot snapshot() const;
    [[nodiscard]] std::size_t staleFiles() const noexcept;
    [[nodiscard]] std::uint64_t staleBytes() const noexcept;

private:
    void work();
    void reset(bool verify, bool stale);

    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<JobPhase> phase_{JobPhase::Idle};
    std::atomic<std::size_t> entryIndex_{0};
    std::atomic<std::size_t> entryCount_{0};
    std::atomic<std::uint64_t> downloaded_{0};
    std::atomic<std::uint64_t> downloadTotal_{0};
    std::atomic<std::uint64_t> hashedBytes_{0};
    std::atomic<JobText> currentFile_;
    std::atomic<JobText> message_;
    std::atomic<bool> verify_{false};
    std::atomic<bool> stale_{false};
    std::atomic<std::size_t> staleFiles_{0};
    std::atomic<std::uint64_t> staleBytes_{0};
};

}
