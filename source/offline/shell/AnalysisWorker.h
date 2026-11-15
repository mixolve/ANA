#pragma once

#include "offline/shell/AnalysisRequest.h"
#include "offline/shell/AnalysisResult.h"

#include <JuceHeader.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace ana::offline
{
class Worker final : private juce::Thread
{
public:
    using ShouldCancel = std::function<bool()>;
    using ProgressCallback = std::function<void(float)>;
    using BuildAnalysis = std::function<std::shared_ptr<AnalysisResult>(
        const AnalysisRequest&, const ShouldCancel&, const ProgressCallback&)>;

    Worker(const juce::String& threadName, BuildAnalysis buildAnalysis);
    ~Worker() override;

    void request(AnalysisRequest request, bool forceRefresh = false);
    void rescheduleLatest(uint64_t regionGeneration);
    void cancel();
    void stop();

    std::shared_ptr<const AnalysisResult> getAnalysisResult() const;
    // -1 means the latest build failed; 0..99 is running, 100 is complete/idle.
    int getProgress() const noexcept { return progress.load(std::memory_order_acquire); }

private:
    void run() override;

    struct CacheEntry
    {
        AnalysisRequest settings;
        std::shared_ptr<const AnalysisResult> result;
    };
    static constexpr size_t maximumCachedAnalyses = 6;

    BuildAnalysis buildAnalysis;
    mutable juce::CriticalSection stateLock;
    std::optional<AnalysisRequest> pendingRequest;
    std::shared_ptr<const AnalysisResult> analysis;
    std::vector<CacheEntry> completedCache;
    AnalysisRequest latestSettings;
    double retryNotBeforeMilliseconds = 0.0;
    std::atomic<uint64_t> latestRevision { 0 };
    std::atomic<int> progress { 100 };
    std::atomic<bool> stopped { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Worker)
};
}
