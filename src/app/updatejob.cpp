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
           std::atomic<std::uint64_t>& total, std::atomic<JobText>& currentFile,
           std::atomic<std::uint64_t>& hashed, bool apply)
        : phase_(phase), index_(index), count_(count), downloaded_(downloaded), total_(total),
          currentFile_(currentFile), hashed_(hashed), apply_(apply)
    {
    }

    void onChecking(std::size_t checked, std::size_t total, std::uint64_t hashedBytes) override
    {
        index_.store(checked, std::memory_order_relaxed);
        count_.store(total, std::memory_order_relaxed);
        hashed_.store(hashedBytes, std::memory_order_relaxed);
    }

    void onPlan(std::size_t queued, std::uint64_t downloadBytes, std::size_t, std::size_t,
                std::size_t, std::size_t) override
    {
        count_.store(queued, std::memory_order_relaxed);
        total_.store(downloadBytes, std::memory_order_relaxed);
        if (queued != 0 && apply_)
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

    // the window has nowhere to show a running log, so only the levels worth a trace survive
    void onLog(core::Level level, std::wstring_view message) override
    {
        if (level != core::Level::Debug)
            core::write(level, core::narrow(message));
    }

private:
    std::atomic<JobPhase>& phase_;
    std::atomic<std::size_t>& index_;
    std::atomic<std::size_t>& count_;
    std::atomic<std::uint64_t>& downloaded_;
    std::atomic<std::uint64_t>& total_;
    std::atomic<JobText>& currentFile_;
    std::atomic<std::uint64_t>& hashed_;
    bool apply_ = false;
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

void UpdateJob::setTitle(wf::Title title)
{
    title_.store(title, std::memory_order_relaxed);
}

void UpdateJob::cancel()
{
    if (running_.load(std::memory_order_acquire))
        cancel_.request();
}

void UpdateJob::join()
{
    if (thread_.joinable())
        thread_.join();
}

void UpdateJob::reset(bool verify, bool stale, bool apply)
{
    cancel();
    join();
    // must follow join, or a worker still unwinding sees the flag clear and runs on
    cancel_.reset();
    thread_ = std::thread();
    verify_.store(verify, std::memory_order_relaxed);
    stale_.store(stale, std::memory_order_relaxed);
    apply_.store(apply, std::memory_order_relaxed);
    queuedFiles_.store(0, std::memory_order_relaxed);
    queuedBytes_.store(0, std::memory_order_relaxed);
    phase_.store(JobPhase::Idle, std::memory_order_relaxed);
    entryIndex_.store(0, std::memory_order_relaxed);
    entryCount_.store(0, std::memory_order_relaxed);
    downloaded_.store(0, std::memory_order_relaxed);
    downloadTotal_.store(0, std::memory_order_relaxed);
    hashedBytes_.store(0, std::memory_order_relaxed);
    staleFiles_.store(0, std::memory_order_relaxed);
    staleBytes_.store(0, std::memory_order_relaxed);
    currentFile_.store({}, std::memory_order_release);
    message_.store({}, std::memory_order_release);
    start();
}

void UpdateJob::restart(bool verify)
{
    reset(verify, false, false);
}

void UpdateJob::startUpdate()
{
    reset(false, false, true);
}

void UpdateJob::startStaleReport()
{
    reset(false, true, false);
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
    out.hashedBytes = hashedBytes_.load(std::memory_order_relaxed);
    out.queuedFiles = queuedFiles_.load(std::memory_order_relaxed);
    out.queuedBytes = queuedBytes_.load(std::memory_order_relaxed);
    out.verifying = verify_.load(std::memory_order_relaxed);
    out.scanning = stale_.load(std::memory_order_relaxed);
    return out;
}

void UpdateJob::work()
{
    const bool apply = apply_.load(std::memory_order_relaxed);
    Bridge bridge(phase_, entryIndex_, entryCount_, downloaded_, downloadTotal_, currentFile_,
                  hashedBytes_, apply);

    wf::Options options;
    options.config.title = title_.load(std::memory_order_relaxed);
    options.config.launcher = wf::LauncherConfig::load();
    const Settings settings = Settings::load(options.config.title, options.config.launcher);
    options.config.root = settings.installRoot(options.config.branch);
    options.config.language = settings.language;
    options.config.steam = settings.steam();
    options.config.eosSdk = settings.eos();
    options.config.dx12 = settings.dx12();
    options.config.bulkDownload = settings.bulkDownload;
    options.config.forceHttps = !settings.allowNetworkCaches;
    options.config.sideload = settings.sideload;
    options.config.hashCaches = verify_.load(std::memory_order_relaxed);
    options.staleReport = stale_.load(std::memory_order_relaxed);
    // a check builds the plan and stops; only an apply writes files
    options.dryRun = !apply && !options.staleReport;
    options.ctx.progress = &bridge;
    options.ctx.cancel = &cancel_;

    const auto summary = wf::run(options);

    if (summary && summary->mainExe && apply && settings.sideload)
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
    else if (options.dryRun && summary->queued != 0)
    {
        phase = JobPhase::UpdateReady;
    }

    if (summary)
    {
        staleFiles_.store(summary->staleFiles, std::memory_order_relaxed);
        staleBytes_.store(summary->staleBytes, std::memory_order_release);
        queuedFiles_.store(summary->queued, std::memory_order_relaxed);
        queuedBytes_.store(summary->downloadBytes, std::memory_order_release);
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
