#include "EditorRenderer.h"
#include "Analyzer.h"
#include "SourceCatalog.h"


#include <unordered_set>

#include <utility>

namespace ana::offline
{
EditorRenderer::EditorRenderer(ARA::PlugIn::DocumentController* documentController,
                               ProcessingLock& processingLock)
    : juce::ARAEditorRenderer(documentController),
      lock(processingLock),
      offlineDocumentController(documentController),
      analysisWorker("ANA Track Offline Analysis",
                     [this] (const offline::AnalysisRequest& request,
                             const offline::Worker::ShouldCancel& shouldCancel,
                             const offline::Worker::ProgressCallback& onProgress)
                     {
                         return buildAnalysisResult(request, shouldCancel, onProgress);
                     })
{
}

EditorRenderer::~EditorRenderer()
{
    analysisWorker.stop();

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
    analysisWorker.rescheduleLatest(regionGeneration.load(std::memory_order_relaxed));
}

void EditorRenderer::requestOfflineAnalysis(offline::AnalysisRequest request,
                                            const bool forceRefresh)
{
    request.regionGeneration = regionGeneration.load(std::memory_order_relaxed);
    analysisWorker.request(std::move(request), forceRefresh);
}

std::shared_ptr<const offline::AnalysisResult> EditorRenderer::getAnalysisResult() const
{
    return analysisWorker.getAnalysisResult();
}

std::vector<offline::SourceChoice> EditorRenderer::getSourceChoices() const
{
    return sourceChoiceCache.get(lock, [this]
    {
        return makeSourceChoices(collectPlaybackRegions());
    });
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

    return collectAnalysisRegions(offlineDocumentController, regions);
}

std::shared_ptr<offline::AnalysisResult> EditorRenderer::buildAnalysisResult(
    const offline::AnalysisRequest& request,
    const offline::Worker::ShouldCancel& shouldCancel,
    const offline::Worker::ProgressCallback& onProgress)
{
    const juce::ScopedReadLock processingLock(lock.getProcessingReadWriteLock());
    const auto playbackRegions = collectPlaybackRegions();
    return analysePlaybackRegions(
        offlineDocumentController, playbackRegions, request, request.revision | (uint64_t { 1 } << 63),
        shouldCancel, onProgress);
}
}
