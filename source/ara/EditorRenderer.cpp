#include "EditorRenderer.h"

#if JucePlugin_Enable_ARA

#include <algorithm>
#include <limits>
#include <unordered_set>

namespace ana::ara
{
EditorRenderer::EditorRenderer(ARA::PlugIn::DocumentController* documentController,
                               ProcessingLock& processingLock)
    : juce::ARAEditorRenderer(documentController),
      juce::Thread("ANA ARA Track Offline Analysis"),
      lock(processingLock),
      araDocumentController(documentController)
{
    startThread(juce::Thread::Priority::high);
}

EditorRenderer::~EditorRenderer()
{
    signalThreadShouldExit();
    notify();
    stopThread(5000);

    for (auto* regionSequence : listenedRegionSequences)
        regionSequence->removeListener(this);
}

void EditorRenderer::didAddPlaybackRegion(ARA::PlugIn::PlaybackRegion*) noexcept
{
    regionsChanged();
}

void EditorRenderer::didRemovePlaybackRegion(ARA::PlugIn::PlaybackRegion*) noexcept
{
    regionsChanged();
}

void EditorRenderer::didAddRegionSequence(ARA::PlugIn::RegionSequence* regionSequence) noexcept
{
    auto* sequence = static_cast<juce::ARARegionSequence*>(regionSequence);
    sequence->addListener(this);
    listenedRegionSequences.insert(sequence);
    regionsChanged();
}

void EditorRenderer::willRemoveRegionSequence(ARA::PlugIn::RegionSequence* regionSequence) noexcept
{
    auto* sequence = static_cast<juce::ARARegionSequence*>(regionSequence);
    sequence->removeListener(this);
    listenedRegionSequences.erase(sequence);
    regionsChanged();
}

void EditorRenderer::didAddPlaybackRegionToRegionSequence(juce::ARARegionSequence*,
                                                           juce::ARAPlaybackRegion*)
{
    regionsChanged();
}

void EditorRenderer::willRemovePlaybackRegionFromRegionSequence(juce::ARARegionSequence*,
                                                                 juce::ARAPlaybackRegion*)
{
    regionsChanged();
}

void EditorRenderer::regionsChanged()
{
    regionGeneration.fetch_add(1, std::memory_order_relaxed);
    scheduleLatestAnalysis();
}

void EditorRenderer::requestOfflineAnalysis(OfflineAnalysisRequest request,
                                            const bool forceRefresh)
{
    request.regionGeneration = regionGeneration.load(std::memory_order_relaxed);
    const juce::ScopedLock scopedLock(analysisLock);

    if (! forceRefresh
        && latestAnalysisSettings.revision != 0
        && latestAnalysisSettings.hasSameSettings(request))
        return;

    request.revision = latestAnalysisRevision.fetch_add(1, std::memory_order_relaxed) + 1;
    latestAnalysisSettings = std::move(request);
    pendingAnalysis = latestAnalysisSettings;
    analysisProgress.store(0, std::memory_order_release);

    notify();
}

std::shared_ptr<const OfflineAnalysisSnapshot> EditorRenderer::getOfflineSnapshot() const
{
    const juce::ScopedLock scopedLock(analysisLock);
    return offlineSnapshot;
}

std::vector<OfflineSourceTakeChoice> EditorRenderer::getOfflineSourceTakeChoices() const
{
    const auto processingLock = lock.getProcessingLock();

    if (! processingLock.isLocked())
    {
        const juce::ScopedLock scopedLock(analysisLock);
        return cachedOfflineSourceTakeChoices;
    }

    auto choices = makeOfflineSourceTakeChoices(araDocumentController, collectPlaybackRegions());

    {
        const juce::ScopedLock scopedLock(analysisLock);
        cachedOfflineSourceTakeChoices = choices;
    }

    return choices;
}

void EditorRenderer::scheduleLatestAnalysis()
{
    const juce::ScopedLock scopedLock(analysisLock);

    if (latestAnalysisSettings.revision == 0)
        return;

    latestAnalysisSettings.regionGeneration = regionGeneration.load(std::memory_order_relaxed);
    latestAnalysisSettings.revision = latestAnalysisRevision.fetch_add(1, std::memory_order_relaxed) + 1;
    pendingAnalysis = latestAnalysisSettings;
    analysisProgress.store(0, std::memory_order_release);
    notify();
}

void EditorRenderer::run()
{
    while (! threadShouldExit())
    {
        wait(-1);

        if (threadShouldExit())
            break;

        std::optional<OfflineAnalysisRequest> request;

        {
            const juce::ScopedLock scopedLock(analysisLock);
            request = pendingAnalysis;
            pendingAnalysis.reset();
        }

        if (! request.has_value())
            continue;

        auto snapshot = buildOfflineSnapshot(*request);

        if (snapshot != nullptr
            && request->revision == latestAnalysisRevision.load(std::memory_order_relaxed))
        {
            const juce::ScopedLock scopedLock(analysisLock);
            offlineSnapshot = std::move(snapshot);
            analysisProgress.store(100, std::memory_order_release);
        }
        else if (snapshot == nullptr
                 && request->revision == latestAnalysisRevision.load(std::memory_order_relaxed)
                 && ! threadShouldExit())
        {
            wait(20);
            const juce::ScopedLock scopedLock(analysisLock);
            pendingAnalysis = request;
            notify();
        }
    }
}

std::vector<juce::ARAPlaybackRegion*> EditorRenderer::collectPlaybackRegions() const
{
    std::vector<juce::ARAPlaybackRegion*> regions;
    std::unordered_set<juce::ARAPlaybackRegion*> seen;

    const auto append = [&regions, &seen] (juce::ARAPlaybackRegion* playbackRegion)
    {
        if (playbackRegion != nullptr && seen.insert(playbackRegion).second)
            regions.push_back(playbackRegion);
    };

    for (auto* playbackRegion : getPlaybackRegions())
        append(playbackRegion);

    for (auto* regionSequence : getRegionSequences())
        for (auto* playbackRegion : regionSequence->getPlaybackRegions())
            append(playbackRegion);

    return collectOfflinePlaybackRegions(araDocumentController, regions);
}

std::shared_ptr<OfflineAnalysisSnapshot> EditorRenderer::buildOfflineSnapshot(
    const OfflineAnalysisRequest& request)
{
    const juce::ScopedReadLock processingLock(lock.getProcessingReadWriteLock());
    const auto playbackRegions = collectPlaybackRegions();
    return analyseOfflinePlaybackRegions(
        araDocumentController, playbackRegions, request, request.revision | (uint64_t { 1 } << 63),
        [this, &request]
        {
            return threadShouldExit()
                || request.revision != latestAnalysisRevision.load(std::memory_order_relaxed);
        },
        [this] (const float progress)
        {
            analysisProgress.store(juce::jlimit(0, 99, juce::roundToInt(progress * 100.0f)),
                                   std::memory_order_release);
        });
}
} // namespace ana::ara

#endif
