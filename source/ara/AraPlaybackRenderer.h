#pragma once

#include "../MultibandScope.h"

#include <JuceHeader.h>

#if JucePlugin_Enable_ARA

#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace ana::ara
{
juce::String getOfflineSourceId(const juce::ARAPlaybackRegion& playbackRegion);
juce::String getOfflineTakeId(const juce::ARAPlaybackRegion& playbackRegion);
int getOfflineTakeNumber(const juce::ARAPlaybackRegion& playbackRegion,
                         const std::vector<OfflineSourceTakeChoice>& choices);
bool matchesOfflineSelection(const juce::ARAPlaybackRegion& playbackRegion,
                             const std::vector<OfflineSourceTakeChoice>& choices,
                             const juce::String& sourceId,
                             const juce::String& takeSelection);
std::vector<juce::ARAPlaybackRegion*> collectOfflinePlaybackRegions(
    ARA::PlugIn::DocumentController* documentController,
    const std::vector<juce::ARAPlaybackRegion*>& rendererRegions);
std::vector<OfflineSourceTakeChoice> makeOfflineSourceTakeChoices(
    ARA::PlugIn::DocumentController* documentController,
    const std::vector<juce::ARAPlaybackRegion*>& playbackRegions);
juce::ARAAudioModification* findOfflineTakeModification(
    ARA::PlugIn::DocumentController* documentController,
    const std::vector<OfflineSourceTakeChoice>& choices,
    const juce::String& sourceId,
    const juce::String& takeSelection);
bool analyseOfflineTakeSource(OfflineScopeSnapshot& snapshot,
                              juce::ARAAudioSource& source,
                              const dsp::Crossover::SplitFrequencies& frequencies,
                              size_t activeSplitCount,
                              const std::function<bool()>& shouldCancel);
bool analyseHostTakeChoices(OfflineScopeSnapshot& snapshot,
                            ARA::PlugIn::DocumentController* documentController,
                            const std::vector<OfflineSourceTakeChoice>& choices,
                            const juce::String& sourceId,
                            const juce::String& takeSelection,
                            const dsp::Crossover::SplitFrequencies& frequencies,
                            size_t activeSplitCount,
                            size_t columnCount,
                            const std::function<bool()>& shouldCancel);

class ProcessingLock
{
public:
    virtual ~ProcessingLock() = default;
    virtual juce::ScopedTryReadLock getProcessingLock() = 0;
};

class PlaybackRenderer final : public juce::ARAPlaybackRenderer,
                               private juce::Thread
{
public:
    PlaybackRenderer(ARA::PlugIn::DocumentController* documentController,
                     ProcessingLock& processingLock);
    ~PlaybackRenderer() override;

    void prepareToPlay(double sampleRate,
                       int maximumSamplesPerBlock,
                       int numChannels,
                       juce::AudioProcessor::ProcessingPrecision precision,
                       AlwaysNonRealtime alwaysNonRealtime) override;
    void releaseResources() override;

    bool processBlock(juce::AudioBuffer<float>& buffer,
                      juce::AudioProcessor::Realtime realtime,
                      const juce::AudioPlayHead::PositionInfo& positionInfo) noexcept override;

    void requestOfflineAnalysis(size_t activeSplitCount,
                                const dsp::Crossover::SplitFrequencies& frequencies,
                                size_t columnCount,
                                const juce::String& sourceId,
                                const juce::String& takeId,
                                const std::vector<OfflineSourceTakeChoice>& sourceTakeChoices,
                                bool forceRefresh = false);
    std::shared_ptr<const OfflineScopeSnapshot> getOfflineSnapshot() const;
    std::vector<OfflineSourceTakeChoice> getOfflineSourceTakeChoices() const;

    using juce::ARAPlaybackRenderer::processBlock;

protected:
    void didAddPlaybackRegion(ARA::PlugIn::PlaybackRegion* playbackRegion) noexcept override;
    void didRemovePlaybackRegion(ARA::PlugIn::PlaybackRegion* playbackRegion) noexcept override;

private:
    class SharedReaderThread;
    class AudioSourceReader;

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

    void run() override;
    void rebuildReaders();
    void scheduleLatestAnalysis();
    std::shared_ptr<OfflineScopeSnapshot> buildOfflineSnapshot(const AnalysisRequest& request);

    ProcessingLock& lock;
    ARA::PlugIn::DocumentController* araDocumentController = nullptr;
    juce::SharedResourcePointer<SharedReaderThread> sharedReaderThread;
    std::map<juce::ARAAudioSource*, std::unique_ptr<AudioSourceReader>> readers;
    std::unique_ptr<juce::AudioBuffer<float>> mixBuffer;
    std::unique_ptr<juce::AudioBuffer<float>> sourceBuffer;
    mutable juce::CriticalSection analysisLock;
    std::optional<AnalysisRequest> pendingAnalysis;
    std::shared_ptr<const OfflineScopeSnapshot> offlineSnapshot;
    mutable std::vector<OfflineSourceTakeChoice> cachedOfflineSourceTakeChoices;
    AnalysisRequest latestAnalysisSettings;
    std::atomic<uint64_t> latestAnalysisRevision { 0 };
    std::atomic<uint64_t> regionGeneration { 0 };
    bool useBufferedReaders = true;
    bool isPrepared = false;
    int preparedChannelCount = 2;
    int preparedBlockSize = 512;
    int sourceBufferSize = 520;
    double preparedSampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PlaybackRenderer)
};
} // namespace ana::ara

#endif
