#include "app/updatejob.h"

#include "app/settings.h"
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
    options.progress = &bridge;

    const auto summary = wf::run(options);

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

    if (!message.empty())
        message_.store(std::make_shared<const std::string>(std::move(message)),
                       std::memory_order_release);
    phase_.store(phase, std::memory_order_release);
    running_.store(false, std::memory_order_release);
}

}
