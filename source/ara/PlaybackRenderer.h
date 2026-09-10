#pragma once

#include "OfflineAnalysisData.h"
#include "../lvls/MeterProcessor.h"

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
bool analyseOfflineTakeSource(OfflineAnalysisSnapshot& snapshot,
                              juce::ARAAudioSource& source,
                              const dsp::Crossover::SplitFrequencies& frequencies,
                              size_t activeSplitCount,
                              bool includeScope,
                              bool includeFrequency,
                              bool includeCorrelation,
                              bool includeLevel,
                              lvls::MeterProcessor::ProcessingOptions levelOptions,
                              const std::function<bool()>& shouldCancel,
                              const std::function<void(float)>& onProgress);
bool analyseHostTakeChoices(OfflineAnalysisSnapshot& snapshot,
                            ARA::PlugIn::DocumentController* documentController,
                            const std::vector<OfflineSourceTakeChoice>& choices,
                            const juce::String& sourceId,
                            const juce::String& takeSelection,
                            const dsp::Crossover::SplitFrequencies& frequencies,
                            size_t activeSplitCount,
                            size_t columnCount,
                            bool includeScope,
                            bool includeFrequency,
                            bool includeCorrelation,
                            bool includeLevel,
                            lvls::MeterProcessor::ProcessingOptions levelOptions,
                            const std::function<bool()>& shouldCancel,
                            const std::function<void(float)>& onProgress);
void prepareOfflineFrequencySpectrum(OfflineAnalysisSnapshot& snapshot,
                                     int blockSize,
                                     float overlap,
                                     float averagingTimeMilliseconds);
void processOfflineFrequencyBlock(OfflineAnalysisSnapshot& snapshot,
                                  juce::AudioBuffer<float>& buffer,
                                  int samplesToProcess,
                                  double sampleRate);
void prepareOfflineCorrelationSpectrum(OfflineAnalysisSnapshot& snapshot,
                                       int blockSize,
                                       float overlap,
                                       float averagingTimeMilliseconds);
void processOfflineCorrelationBlock(OfflineAnalysisSnapshot& snapshot,
                                    juce::AudioBuffer<float>& buffer,
                                    int samplesToProcess,
                                    double sampleRate);
void prepareOfflineLevelMeter(OfflineAnalysisSnapshot& snapshot);
void processOfflineLevelBlock(OfflineAnalysisSnapshot& snapshot,
                              juce::AudioBuffer<float>& buffer,
                              int samplesToProcess,
                              double sampleRate,
                              lvls::MeterProcessor::ProcessingOptions options = {});
std::shared_ptr<OfflineAnalysisSnapshot> analyseOfflinePlaybackRegions(
    ARA::PlugIn::DocumentController* documentController,
    const std::vector<juce::ARAPlaybackRegion*>& playbackRegions,
    const OfflineAnalysisRequest& request,
    uint64_t snapshotRevision,
    const std::function<bool()>& shouldCancel,
    const std::function<void(float)>& onProgress);

class ProcessingLock
{
public:
    virtual ~ProcessingLock() = default;
    virtual juce::ReadWriteLock& getProcessingReadWriteLock() = 0;
    juce::ScopedTryReadLock getProcessingLock()
    {
        return juce::ScopedTryReadLock(getProcessingReadWriteLock());
    }
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

    void requestOfflineAnalysis(OfflineAnalysisRequest request, bool forceRefresh = false);
    std::shared_ptr<const OfflineAnalysisSnapshot> getOfflineSnapshot() const;
    std::vector<OfflineSourceTakeChoice> getOfflineSourceTakeChoices() const;
    bool setRealtimeTakeChoices(const std::vector<OfflineSourceTakeChoice>& choices);
    int getAnalysisProgress() const noexcept { return analysisProgress.load(std::memory_order_acquire); }

    using juce::ARAPlaybackRenderer::processBlock;

protected:
    void didAddPlaybackRegion(ARA::PlugIn::PlaybackRegion* playbackRegion) noexcept override;
    void didRemovePlaybackRegion(ARA::PlugIn::PlaybackRegion* playbackRegion) noexcept override;

private:
    class SharedReaderThread;
    class AudioSourceReader;

    struct RealtimeTake
    {
        juce::ARAAudioSource* audioSource = nullptr;
        double playbackStartSeconds = 0.0;
        double playbackDurationSeconds = 0.0;
        double sourceStartSeconds = 0.0;
        double playRate = 1.0;
        double gain = 1.0;
    };

    void run() override;
    void rebuildReaders();
    void scheduleLatestAnalysis();
    std::shared_ptr<OfflineAnalysisSnapshot> buildOfflineSnapshot(const OfflineAnalysisRequest& request);

    ProcessingLock& lock;
    ARA::PlugIn::DocumentController* araDocumentController = nullptr;
    juce::SharedResourcePointer<SharedReaderThread> sharedReaderThread;
    std::map<juce::ARAAudioSource*, std::unique_ptr<AudioSourceReader>> readers;
    std::unique_ptr<juce::AudioBuffer<float>> renderedBuffer;
    std::unique_ptr<juce::AudioBuffer<float>> mixBuffer;
    std::unique_ptr<juce::AudioBuffer<float>> sourceBuffer;
    std::atomic<bool> hostTakeSelectionPresent { false };
    mutable juce::CriticalSection analysisLock;
    std::optional<OfflineAnalysisRequest> pendingAnalysis;
    std::shared_ptr<const OfflineAnalysisSnapshot> offlineSnapshot;
    mutable std::vector<OfflineSourceTakeChoice> cachedOfflineSourceTakeChoices;
    OfflineAnalysisRequest latestAnalysisSettings;
    std::atomic<uint64_t> latestAnalysisRevision { 0 };
    std::atomic<int> analysisProgress { 100 };
    std::atomic<uint64_t> regionGeneration { 0 };
    std::shared_ptr<const std::vector<RealtimeTake>> realtimeTakes;
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
