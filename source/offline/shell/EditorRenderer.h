#pragma once

#include "AnalysisWorker.h"
#include "SourceChoiceCache.h"
#include "ProcessingLock.h"
#include "offline/shell/AnalysisRequest.h"
#include "offline/shell/AnalysisResult.h"
#include "offline/shell/SourceChoice.h"


#include <cstdint>
#include <atomic>
#include <memory>
#include <set>
#include <vector>

namespace ana::offline
{
class EditorRenderer final : public juce::ARAEditorRenderer,
                             private juce::ARARegionSequence::Listener
{
public:
    EditorRenderer(ARA::PlugIn::DocumentController* documentController,
                   ProcessingLock& processingLock);
    ~EditorRenderer() override;

    void requestOfflineAnalysis(offline::AnalysisRequest request, bool forceRefresh = false);
    void cancelAnalysis() { analysisWorker.cancel(); }
    std::shared_ptr<const offline::AnalysisResult> getAnalysisResult() const;
    std::vector<offline::SourceChoice> getSourceChoices() const;
    int getAnalysisProgress() const noexcept { return analysisWorker.getProgress(); }

protected:
    void didAddPlaybackRegion(ARA::PlugIn::PlaybackRegion* playbackRegion) noexcept override;
    void didRemovePlaybackRegion(ARA::PlugIn::PlaybackRegion* playbackRegion) noexcept override;
    void didAddRegionSequence(ARA::PlugIn::RegionSequence* regionSequence) noexcept override;
    void willRemoveRegionSequence(ARA::PlugIn::RegionSequence* regionSequence) noexcept override;

private:
    void didAddPlaybackRegionToRegionSequence(juce::ARARegionSequence* regionSequence,
                                               juce::ARAPlaybackRegion* playbackRegion) override;
    void willRemovePlaybackRegionFromRegionSequence(juce::ARARegionSequence* regionSequence,
                                                     juce::ARAPlaybackRegion* playbackRegion) override;
    void regionsChanged();
    std::vector<juce::ARAPlaybackRegion*> collectPlaybackRegions() const;
    std::shared_ptr<offline::AnalysisResult> buildAnalysisResult(
        const offline::AnalysisRequest& request,
        const offline::Worker::ShouldCancel& shouldCancel,
        const offline::Worker::ProgressCallback& onProgress);

    ProcessingLock& lock;
    ARA::PlugIn::DocumentController* offlineDocumentController = nullptr;
    std::set<juce::ARARegionSequence*> listenedRegionSequences;
    mutable SourceChoiceCache sourceChoiceCache;
    std::atomic<uint64_t> regionGeneration { 0 };
    offline::Worker analysisWorker;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EditorRenderer)
};
}
