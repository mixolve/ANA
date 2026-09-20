#pragma once

#include "AraWorker.h"
#include "AraSourceChoiceCache.h"
#include "ProcessingLock.h"
#include "shell/AraAnalysisRequest.h"
#include "shell/AraAnalysisResult.h"
#include "shell/AraSourceChoice.h"

#if JucePlugin_Enable_ARA

#include <cstdint>
#include <atomic>
#include <memory>
#include <set>
#include <vector>

namespace ana::ara
{
class EditorRenderer final : public juce::ARAEditorRenderer,
                             private juce::ARARegionSequence::Listener
{
public:
    EditorRenderer(ARA::PlugIn::DocumentController* documentController,
                   ProcessingLock& processingLock);
    ~EditorRenderer() override;

    void requestAraAnalysis(ara::AnalysisRequest request, bool forceRefresh = false);
    std::shared_ptr<const ara::AnalysisResult> getAraAnalysisResult() const;
    std::vector<ara::SourceChoice> getAraSourceChoices() const;
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
    std::shared_ptr<ara::AnalysisResult> buildAraAnalysisResult(
        const ara::AnalysisRequest& request,
        const ara::Worker::ShouldCancel& shouldCancel,
        const ara::Worker::ProgressCallback& onProgress);

    ProcessingLock& lock;
    ARA::PlugIn::DocumentController* araDocumentController = nullptr;
    std::set<juce::ARARegionSequence*> listenedRegionSequences;
    mutable AraSourceChoiceCache sourceChoiceCache;
    std::atomic<uint64_t> regionGeneration { 0 };
    ara::Worker analysisWorker;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EditorRenderer)
};
}

#endif
