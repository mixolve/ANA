#include "AnalysisWorker.h"

#include <utility>

namespace ana::offline
{
Worker::Worker(const juce::String& threadName,
               BuildAnalysis buildAnalysisIn)
    : juce::Thread(threadName),
      buildAnalysis(std::move(buildAnalysisIn))
{
    startThread(juce::Thread::Priority::high);
}

Worker::~Worker()
{
    stop();
}

void Worker::stop()
{
    if (stopped.exchange(true, std::memory_order_acq_rel))
        return;

    signalThreadShouldExit();
    notify();
    stopThread(-1);
}

void Worker::request(AnalysisRequest request, const bool forceRefresh)
{
    const juce::ScopedLock scopedLock(stateLock);
    if (stopped.load(std::memory_order_acquire))
        return;

    if (! forceRefresh
        && latestSettings.revision != 0
        && latestSettings.hasSameSettings(request)
        && (progress.load(std::memory_order_relaxed) != -1
            || juce::Time::getMillisecondCounterHiRes() < retryNotBeforeMilliseconds))
        return;

    // Returning to an already analysed page is a view change, not a media edit.
    // A matching cached result must not schedule a new background scan.
    if (forceRefresh)
        completedCache.clear();
    else
        for (auto it = completedCache.begin(); it != completedCache.end(); ++it)
            if (it->settings.hasSameSettings(request))
            {
                request.revision = latestRevision.fetch_add(1, std::memory_order_relaxed) + 1;
                latestSettings = std::move(request);
                analysis = it->result;
                pendingRequest.reset();
                progress.store(100, std::memory_order_release);
                // Most recently used result stays in cache when more pages are visited.
                if (it + 1 != completedCache.end())
                {
                    auto entry = std::move(*it);
                    completedCache.erase(it);
                    completedCache.push_back(std::move(entry));
                }
                return;
            }

    request.revision = latestRevision.fetch_add(1, std::memory_order_relaxed) + 1;
    latestSettings = std::move(request);
    pendingRequest = latestSettings;
    progress.store(0, std::memory_order_release);
    notify();
}

void Worker::cancel()
{
    const juce::ScopedLock scopedLock(stateLock);
    latestRevision.fetch_add(1, std::memory_order_relaxed);
    pendingRequest.reset();
    latestSettings.revision = 0;
    progress.store(100, std::memory_order_release);
}

void Worker::rescheduleLatest(const uint64_t regionGeneration)
{
    const juce::ScopedLock scopedLock(stateLock);

    if (stopped.load(std::memory_order_acquire) || latestSettings.revision == 0)
        return;

    // Host-enumerated source state is authoritative; transient ARA region churn must not rebuild on focus changes.
    if (latestSettings.hasHostSourceChoices())
        return;

    latestSettings.regionGeneration = regionGeneration;
    latestSettings.revision = latestRevision.fetch_add(1, std::memory_order_relaxed) + 1;
    pendingRequest = latestSettings;
    progress.store(0, std::memory_order_release);
    notify();
}

std::shared_ptr<const AnalysisResult> Worker::getAnalysisResult() const
{
    const juce::ScopedLock scopedLock(stateLock);
    return analysis;
}

void Worker::run()
{
    while (! threadShouldExit())
    {
        wait(-1);

        if (threadShouldExit())
            break;

        std::optional<AnalysisRequest> requestToBuild;
        {
            const juce::ScopedLock scopedLock(stateLock);
            requestToBuild = pendingRequest;
            pendingRequest.reset();
        }

        if (! requestToBuild.has_value())
            continue;

        const auto revision = requestToBuild->revision;
        const ShouldCancel shouldCancel = [this, revision]
        {
            return threadShouldExit()
                || revision != latestRevision.load(std::memory_order_relaxed);
        };
        const ProgressCallback onProgress = [this, revision] (const float value)
        {
            const juce::ScopedLock scopedLock(stateLock);
            if (revision == latestRevision.load(std::memory_order_relaxed))
                progress.store(juce::jlimit(0, 99, juce::roundToInt(value * 100.0f)),
                               std::memory_order_release);
        };

        auto builtAnalysis = buildAnalysis != nullptr
            ? buildAnalysis(*requestToBuild, shouldCancel, onProgress)
            : nullptr;

        const juce::ScopedLock scopedLock(stateLock);
        if (revision == latestRevision.load(std::memory_order_relaxed))
        {
            if (builtAnalysis != nullptr)
            {
                analysis = std::move(builtAnalysis);
                // Cache completed pages separately: SCOP / SPEC FREQ / SPEC MAP /
                // CORR / LVLS. Settings and source identity are checked on reuse.
                for (auto it = completedCache.begin(); it != completedCache.end(); ++it)
                    if (it->settings.hasSameSettings(*requestToBuild))
                    {
                        completedCache.erase(it);
                        break;
                    }
                completedCache.push_back({ *requestToBuild, analysis });
                if (completedCache.size() > maximumCachedAnalyses)
                    completedCache.erase(completedCache.begin());
                progress.store(100, std::memory_order_release);
            }
            else
            {
                // Hosts can temporarily close their audio sources while inactive.
                // Retain the completed result and let an unchanged request retry.
                retryNotBeforeMilliseconds = juce::Time::getMillisecondCounterHiRes() + 1000.0;
                progress.store(-1, std::memory_order_release);
            }
        }
    }
}
}
