#pragma once

#include "AraPlaybackRenderer.h"

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

    void requestOfflineAnalysis(size_t activeSplitCount,
                                const dsp::Crossover::SplitFrequencies& frequencies,
                                size_t columnCount,
                                const juce::String& sourceId,
                                const juce::String& takeId,
                                const std::vector<OfflineSourceTakeChoice>& sourceTakeChoices,
                                bool forceRefresh = false);
    std::shared_ptr<const OfflineScopeSnapshot> getOfflineSnapshot() const;
    std::vector<OfflineSourceTakeChoice> getOfflineSourceTakeChoices() const;

protected:
    void didAddPlaybackRegion(ARA::PlugIn::PlaybackRegion* playbackRegion) noexcept override;
    void didRemovePlaybackRegion(ARA::PlugIn::PlaybackRegion* playbackRegion) noexcept override;
    void didAddRegionSequence(ARA::PlugIn::RegionSequence* regionSequence) noexcept override;
    void willRemoveRegionSequence(ARA::PlugIn::RegionSequence* regionSequence) noexcept override;

private:
    struct AnalysisRequest
    {
        dsp::Crossover::SplitFrequencies frequencies {};
        size_t activeSplitCount = 0;
        size_t columnCount = 512;
        juce::String sourceId;
        juce::String takeId;
        std::vector<OfflineSourceTakeChoice> sourceTakeChoices;
        uint64_t regionGeneration = 0;
        uint64_t revision = 0;
    };

    void didAddPlaybackRegionToRegionSequence(juce::ARARegionSequence* regionSequence,
                                               juce::ARAPlaybackRegion* playbackRegion) override;
    void willRemovePlaybackRegionFromRegionSequence(juce::ARARegionSequence* regionSequence,
                                                     juce::ARAPlaybackRegion* playbackRegion) override;
    void run() override;
    void regionsChanged();
    void scheduleLatestAnalysis();
    std::vector<juce::ARAPlaybackRegion*> collectPlaybackRegions() const;
    std::shared_ptr<OfflineScopeSnapshot> buildOfflineSnapshot(const AnalysisRequest& request);

    ProcessingLock& lock;
    ARA::PlugIn::DocumentController* araDocumentController = nullptr;
    std::set<juce::ARARegionSequence*> listenedRegionSequences;
    mutable juce::CriticalSection analysisLock;
    std::optional<AnalysisRequest> pendingAnalysis;
    std::shared_ptr<const OfflineScopeSnapshot> offlineSnapshot;
    mutable std::vector<OfflineSourceTakeChoice> cachedOfflineSourceTakeChoices;
    AnalysisRequest latestAnalysisSettings;
    std::atomic<uint64_t> latestAnalysisRevision { 0 };
    std::atomic<uint64_t> regionGeneration { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EditorRenderer)
};
} // namespace ana::ara

#endif
