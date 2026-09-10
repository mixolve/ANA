#pragma once

#include "PlaybackRenderer.h"

#if JucePlugin_Enable_ARA

#include <optional>
#include <set>
#include <vector>

namespace ana::ara
{
class EditorRenderer final : public juce::ARAEditorRenderer,
                             private juce::ARARegionSequence::Listener,
                             private juce::Thread
{
public:
    EditorRenderer(ARA::PlugIn::DocumentController* documentController,
                   ProcessingLock& processingLock);
    ~EditorRenderer() override;

    void requestOfflineAnalysis(OfflineAnalysisRequest request, bool forceRefresh = false);
    std::shared_ptr<const OfflineAnalysisSnapshot> getOfflineSnapshot() const;
    std::vector<OfflineSourceTakeChoice> getOfflineSourceTakeChoices() const;
    int getAnalysisProgress() const noexcept { return analysisProgress.load(std::memory_order_acquire); }

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
    void run() override;
    void regionsChanged();
    void scheduleLatestAnalysis();
    std::vector<juce::ARAPlaybackRegion*> collectPlaybackRegions() const;
    std::shared_ptr<OfflineAnalysisSnapshot> buildOfflineSnapshot(const OfflineAnalysisRequest& request);

    ProcessingLock& lock;
    ARA::PlugIn::DocumentController* araDocumentController = nullptr;
    std::set<juce::ARARegionSequence*> listenedRegionSequences;
    mutable juce::CriticalSection analysisLock;
    std::optional<OfflineAnalysisRequest> pendingAnalysis;
    std::shared_ptr<const OfflineAnalysisSnapshot> offlineSnapshot;
    mutable std::vector<OfflineSourceTakeChoice> cachedOfflineSourceTakeChoices;
    OfflineAnalysisRequest latestAnalysisSettings;
    std::atomic<uint64_t> latestAnalysisRevision { 0 };
    std::atomic<int> analysisProgress { 100 };
    std::atomic<uint64_t> regionGeneration { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EditorRenderer)
};
} // namespace ana::ara

#endif
