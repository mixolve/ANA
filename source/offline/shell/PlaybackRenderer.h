#pragma once

#include "AnalysisWorker.h"
#include "SourceChoiceCache.h"
#include "ProcessingLock.h"
#include "offline/shell/AnalysisRequest.h"
#include "offline/shell/AnalysisResult.h"
#include "offline/shell/SourceChoice.h"

#include <cstdint>
#include <JuceHeader.h>


#include <atomic>
#include <map>
#include <memory>
#include <vector>

namespace ana::offline
{
class PlaybackRenderer final : public juce::ARAPlaybackRenderer
{
public:
    PlaybackRenderer(ARA::PlugIn::DocumentController* documentController,
                     ProcessingLock& processingLock);
    ~PlaybackRenderer() override;

    void prepareToPlay(double sampleRate,
                       int maximumSamplesPerBlock,
                       int numChannels,
                       juce::AudioProcessor::ProcessingPrecision precision,
                       AlwaysNonRealtime alwaysNonRt) override;
    void releaseResources() override;

    bool processBlock(juce::AudioBuffer<float>& buffer,
                      juce::AudioProcessor::Realtime rt,
                      const juce::AudioPlayHead::PositionInfo& positionInfo) noexcept override;

    void requestOfflineAnalysis(offline::AnalysisRequest request, bool forceRefresh = false);
    void cancelAnalysis() { analysisWorker.cancel(); }
    std::shared_ptr<const offline::AnalysisResult> getAnalysisResult() const;
    std::vector<offline::SourceChoice> getSourceChoices() const;
    bool setHostSourceChoices(const std::vector<offline::SourceChoice>& choices);
    int getAnalysisProgress() const noexcept { return analysisWorker.getProgress(); }

    using juce::ARAPlaybackRenderer::processBlock;

protected:
    void didAddPlaybackRegion(ARA::PlugIn::PlaybackRegion* playbackRegion) noexcept override;
    void didRemovePlaybackRegion(ARA::PlugIn::PlaybackRegion* playbackRegion) noexcept override;

private:
    class SharedReaderThread;
    class AudioSourceReader;

    struct RtTake
    {
        juce::ARAAudioSource* audioSource = nullptr;
        double playbackStartSeconds = 0.0;
        double playbackDurationSeconds = 0.0;
        double sourceStartSeconds = 0.0;
        double playRate = 1.0;
        double gain = 1.0;
    };

    void rebuildReaders();
    void playbackRegionsChanged() noexcept;
    std::vector<juce::ARAPlaybackRegion*> collectPlaybackRegions() const;
    std::shared_ptr<offline::AnalysisResult> buildAnalysisResult(
        const offline::AnalysisRequest& request,
        const offline::Worker::ShouldCancel& shouldCancel,
        const offline::Worker::ProgressCallback& onProgress);

    ProcessingLock& lock;
    ARA::PlugIn::DocumentController* offlineDocumentController = nullptr;
    juce::SharedResourcePointer<SharedReaderThread> sharedReaderThread;
    std::map<juce::ARAAudioSource*, std::unique_ptr<AudioSourceReader>> readers;
    std::unique_ptr<juce::AudioBuffer<float>> renderedBuffer;
    std::unique_ptr<juce::AudioBuffer<float>> mixBuffer;
    std::unique_ptr<juce::AudioBuffer<float>> sourceBuffer;
    std::atomic<bool> hostTakeSelectionPresent { false };
    mutable SourceChoiceCache sourceChoiceCache;
    std::atomic<uint64_t> regionGeneration { 0 };
    std::shared_ptr<const std::vector<RtTake>> rtTakes;
    bool useBufferedReaders = true;
    bool isPrepared = false;
    int preparedChannelCount = 2;
    int preparedBlockSize = 512;
    int sourceBufferSize = 520;
    double preparedSampleRate = 44100.0;
    offline::Worker analysisWorker;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PlaybackRenderer)
};
}
