#include "app/updatejob.h"

#include "app/settings.h"
#include "app/sideload.h"
#include "core/cancel.h"
#include "core/str.h"
#include "update/progress.h"
#include "update/updater.h"

#include <utility>

namespace app
{
namespace
{

class Bridge final : public wf::Progress
{
public:
    Bridge(std::atomic<JobPhase>& phase, std::atomic<std::size_t>& index,
           std::atomic<std::size_t>& count, std::atomic<std::uint64_t>& downloaded,
           std::atomic<std::uint64_t>& total, std::atomic<JobText>& currentFile)
        : phase_(phase), index_(index), count_(count), downloaded_(downloaded), total_(total),
          currentFile_(currentFile)
    {
    }

    void onPlan(std::size_t queued, std::uint64_t downloadBytes) override
    {
        count_.store(queued, std::memory_order_relaxed);
        total_.store(downloadBytes, std::memory_order_relaxed);
        if (queued != 0)
            phase_.store(JobPhase::Updating, std::memory_order_relaxed);
    }

    void onEntryStart(std::size_t index, std::size_t count, std::wstring_view installPath,
                      std::uint64_t) override
    {
        currentFile_.store(std::make_shared<const std::string>(core::narrow(installPath)),
                           std::memory_order_release);
        index_.store(index, std::memory_order_release);
        count_.store(count, std::memory_order_relaxed);
    }

    void onBytes(std::uint64_t bytes) override
    {
        downloaded_.fetch_add(bytes, std::memory_order_relaxed);
    }

private:
    std::atomic<JobPhase>& phase_;
    std::atomic<std::size_t>& index_;
    std::atomic<std::size_t>& count_;
    std::atomic<std::uint64_t>& downloaded_;
    std::atomic<std::uint64_t>& total_;
    std::atomic<JobText>& currentFile_;
};

}

UpdateJob::~UpdateJob()
{
    cancel();
    join();
}

void UpdateJob::start()
{
    if (thread_.joinable() || running_.load(std::memory_order_acquire))
        return;
    running_.store(true, std::memory_order_release);
    phase_.store(JobPhase::Checking, std::memory_order_relaxed);
    thread_ = std::thread(&UpdateJob::work, this);
}

void UpdateJob::cancel()
{
    if (running_.load(std::memory_order_acquire))
        core::requestCancel();
}

void UpdateJob::join()
{
    if (thread_.joinable())
        thread_.join();
}

void UpdateJob::reset(bool verify, bool stale)
{
    cancel();
    join();
    // must follow join, or a worker still unwinding sees the flag clear and runs on
    core::resetCancel();
    thread_ = std::thread();
    verify_.store(verify, std::memory_order_relaxed);
    stale_.store(stale, std::memory_order_relaxed);
    phase_.store(JobPhase::Idle, std::memory_order_relaxed);
    entryIndex_.store(0, std::memory_order_relaxed);
    entryCount_.store(0, std::memory_order_relaxed);
    downloaded_.store(0, std::memory_order_relaxed);
    downloadTotal_.store(0, std::memory_order_relaxed);
    staleFiles_.store(0, std::memory_order_relaxed);
    staleBytes_.store(0, std::memory_order_relaxed);
    currentFile_.store({}, std::memory_order_release);
    message_.store({}, std::memory_order_release);
    start();
}

void UpdateJob::restart(bool verify)
{
    reset(verify, false);
}

void UpdateJob::startStaleReport()
{
    reset(false, true);
}

bool UpdateJob::running() const noexcept
{
    return running_.load(std::memory_order_acquire);
}

JobSnapshot UpdateJob::snapshot() const
{
    JobSnapshot out;
    // phase first: its acquire pairs with the release in work(), so message is visible with it
    out.phase = phase_.load(std::memory_order_acquire);
    out.message = message_.load(std::memory_order_acquire);
    out.entryIndex = entryIndex_.load(std::memory_order_acquire);
    out.currentFile = currentFile_.load(std::memory_order_acquire);
    out.entryCount = entryCount_.load(std::memory_order_relaxed);
    out.downloaded = downloaded_.load(std::memory_order_relaxed);
    out.downloadTotal = downloadTotal_.load(std::memory_order_relaxed);
    return out;
}

void UpdateJob::work()
{
    Bridge bridge(phase_, entryIndex_, entryCount_, downloaded_, downloadTotal_, currentFile_);

    const Settings settings = Settings::load();
    wf::Options options;
    options.config.root = settings.installRoot(options.config.branch);
    options.config.language = settings.language;
    options.config.steam = settings.steam();
    options.config.eosSdk = settings.eos();
    options.config.dx12 = settings.dx12();
    options.config.forceHttps = !settings.allowNetworkCaches;
    options.config.launcher = wf::LauncherConfig::load();
    options.config.hashCaches = verify_.load(std::memory_order_relaxed);
    options.staleReport = stale_.load(std::memory_order_relaxed);
    options.progress = &bridge;

    const auto summary = wf::run(options);

    if (summary && summary->mainExe && !options.staleReport)
        ensureSideloaded(options.config.launcher, *summary->mainExe, options.config.root);

    JobPhase phase = JobPhase::Ready;
    std::string message;
    if (!summary)
    {
        phase = JobPhase::Failed;
        message = core::narrow(wf::describe(summary.error()));
    }
    else if (summary->cancelled)
    {
        phase = JobPhase::Cancelled;
    }
    else if (summary->failed != 0)
    {
        phase = JobPhase::Failed;
        message = std::to_string(summary->failed) + " files failed";
    }
    else if (options.staleReport)
    {
        // a stale walk never checked the install, so it cannot claim Ready
        phase = JobPhase::Idle;
    }

    if (summary)
    {
        staleFiles_.store(summary->staleFiles, std::memory_order_relaxed);
        staleBytes_.store(summary->staleBytes, std::memory_order_release);
    }

    if (!message.empty())
        message_.store(std::make_shared<const std::string>(std::move(message)),
                       std::memory_order_release);
    phase_.store(phase, std::memory_order_release);
    running_.store(false, std::memory_order_release);
}

std::size_t UpdateJob::staleFiles() const noexcept
{
    return staleFiles_.load(std::memory_order_acquire);
}

std::uint64_t UpdateJob::staleBytes() const noexcept
{
    return staleBytes_.load(std::memory_order_acquire);
}

}
