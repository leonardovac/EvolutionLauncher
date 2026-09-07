#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace app
{

enum class JobPhase
{
    Idle,
    Checking,
    Updating,
    Ready,
    Failed,
    Cancelled
};

struct JobSnapshot
{
    JobPhase phase = JobPhase::Idle;
    std::size_t entryIndex = 0;
    std::size_t entryCount = 0;
    std::uint64_t downloaded = 0;
    std::uint64_t downloadTotal = 0;
    std::string currentFile;
    std::string message;
};

class UpdateJob
{
public:
    ~UpdateJob();

    void start();
    void cancel();
    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] JobSnapshot snapshot() const;

private:
    void work();

    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<JobPhase> phase_{JobPhase::Idle};
    std::atomic<std::size_t> entryIndex_{0};
    std::atomic<std::size_t> entryCount_{0};
    std::atomic<std::uint64_t> downloaded_{0};
    std::atomic<std::uint64_t> downloadTotal_{0};
    mutable std::mutex textMutex_;
    std::string currentFile_;
    std::string message_;
};

}
