#pragma once

#include "shell/AraAnalysisRequest.h"
#include "shell/AraAnalysisResult.h"

#include <JuceHeader.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

namespace ana::ara
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
    void stop();

    std::shared_ptr<const AnalysisResult> getAnalysisResult() const;
    int getProgress() const noexcept { return progress.load(std::memory_order_acquire); }

private:
    void run() override;

    BuildAnalysis buildAnalysis;
    mutable juce::CriticalSection stateLock;
    std::optional<AnalysisRequest> pendingRequest;
    std::shared_ptr<const AnalysisResult> analysis;
    AnalysisRequest latestSettings;
    std::atomic<uint64_t> latestRevision { 0 };
    std::atomic<int> progress { 100 };
    std::atomic<bool> stopped { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Worker)
};
}
