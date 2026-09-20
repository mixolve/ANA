#include "AraWorker.h"

#include <algorithm>
#include <utility>

namespace ana::ara
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
    stopThread(5000);
}

void Worker::request(AnalysisRequest request, const bool forceRefresh)
{
    const juce::ScopedLock scopedLock(stateLock);

    if (! forceRefresh
        && latestSettings.revision != 0
        && latestSettings.hasSameSettings(request))
        return;

    request.revision = latestRevision.fetch_add(1, std::memory_order_relaxed) + 1;
    latestSettings = std::move(request);
    pendingRequest = latestSettings;
    progress.store(0, std::memory_order_release);
    notify();
}

void Worker::rescheduleLatest(const uint64_t regionGeneration)
{
    const juce::ScopedLock scopedLock(stateLock);

    if (latestSettings.revision == 0)
        return;

    const auto hostCatalogOwnsTopology = std::any_of(
        latestSettings.sourceChoices.begin(), latestSettings.sourceChoices.end(),
        [] (const auto& choice) { return choice.hostEnumerated; });

    // Host-enumerated source state is authoritative; transient ARA region churn must not rebuild on focus changes.
    if (hostCatalogOwnsTopology)
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
            if (revision == latestRevision.load(std::memory_order_relaxed))
                progress.store(juce::jlimit(0, 99, juce::roundToInt(value * 100.0f)),
                               std::memory_order_release);
        };

        auto builtAnalysis = buildAnalysis != nullptr
            ? buildAnalysis(*requestToBuild, shouldCancel, onProgress)
            : nullptr;

        if (builtAnalysis != nullptr)
        {
            const juce::ScopedLock scopedLock(stateLock);
            if (revision == latestRevision.load(std::memory_order_relaxed))
            {
                analysis = std::move(builtAnalysis);
                progress.store(100, std::memory_order_release);
            }
        }
    }
}
}
