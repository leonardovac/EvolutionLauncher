#include "app/updatejob.h"

#include "core/cancel.h"
#include "core/str.h"
#include "update/progress.h"
#include "update/updater.h"

namespace app
{
namespace
{

class Bridge final : public wf::Progress
{
public:
    Bridge(std::atomic<JobPhase>& phase, std::atomic<std::size_t>& index, std::atomic<std::size_t>& count,
           std::atomic<std::uint64_t>& downloaded, std::atomic<std::uint64_t>& total,
           std::mutex& textMutex, std::string& currentFile)
        : phase_(phase), index_(index), count_(count), downloaded_(downloaded), total_(total),
          textMutex_(textMutex), currentFile_(currentFile)
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
        index_.store(index, std::memory_order_relaxed);
        count_.store(count, std::memory_order_relaxed);
        const std::lock_guard<std::mutex> lock(textMutex_);
        currentFile_ = core::narrow(installPath);
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
    std::mutex& textMutex_;
    std::string& currentFile_;
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
    out.phase = phase_.load(std::memory_order_relaxed);
    out.entryIndex = entryIndex_.load(std::memory_order_relaxed);
    out.entryCount = entryCount_.load(std::memory_order_relaxed);
    out.downloaded = downloaded_.load(std::memory_order_relaxed);
    out.downloadTotal = downloadTotal_.load(std::memory_order_relaxed);
    const std::lock_guard<std::mutex> lock(textMutex_);
    out.currentFile = currentFile_;
    out.message = message_;
    return out;
}

void UpdateJob::work()
{
    Bridge bridge(phase_, entryIndex_, entryCount_, downloaded_, downloadTotal_, textMutex_, currentFile_);

    wf::Options options;
    options.config.root = wf::defaultRoot(options.config.branch);
    options.config.language = wf::defaultLanguage();
    options.config.steam = wf::defaultSteam();
    options.config.eosSdk = wf::defaultEos();
    options.config.dx12 = wf::defaultDx12();
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

    {
        const std::lock_guard<std::mutex> lock(textMutex_);
        message_ = message;
    }
    phase_.store(phase, std::memory_order_relaxed);
    running_.store(false, std::memory_order_release);
}

}
