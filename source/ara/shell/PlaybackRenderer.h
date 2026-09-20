#pragma once

#include "AraWorker.h"
#include "AraSourceChoiceCache.h"
#include "ProcessingLock.h"
#include "shell/AraAnalysisRequest.h"
#include "shell/AraAnalysisResult.h"
#include "shell/AraSourceChoice.h"

#include <cstdint>
#include <JuceHeader.h>

#if JucePlugin_Enable_ARA

#include <atomic>
#include <map>
#include <memory>
#include <vector>

namespace ana::ara
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

    void requestAraAnalysis(ara::AnalysisRequest request, bool forceRefresh = false);
    std::shared_ptr<const ara::AnalysisResult> getAraAnalysisResult() const;
    std::vector<ara::SourceChoice> getAraSourceChoices() const;
    bool setRtSourceChoices(const std::vector<ara::SourceChoice>& choices);
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
    std::shared_ptr<ara::AnalysisResult> buildAraAnalysisResult(
        const ara::AnalysisRequest& request,
        const ara::Worker::ShouldCancel& shouldCancel,
        const ara::Worker::ProgressCallback& onProgress);

    ProcessingLock& lock;
    ARA::PlugIn::DocumentController* araDocumentController = nullptr;
    juce::SharedResourcePointer<SharedReaderThread> sharedReaderThread;
    std::map<juce::ARAAudioSource*, std::unique_ptr<AudioSourceReader>> readers;
    std::unique_ptr<juce::AudioBuffer<float>> renderedBuffer;
    std::unique_ptr<juce::AudioBuffer<float>> mixBuffer;
    std::unique_ptr<juce::AudioBuffer<float>> sourceBuffer;
    std::atomic<bool> hostTakeSelectionPresent { false };
    mutable AraSourceChoiceCache sourceChoiceCache;
    std::atomic<uint64_t> regionGeneration { 0 };
    std::shared_ptr<const std::vector<RtTake>> rtTakes;
    bool useBufferedReaders = true;
    bool isPrepared = false;
    int preparedChannelCount = 2;
    int preparedBlockSize = 512;
    int sourceBufferSize = 520;
    double preparedSampleRate = 44100.0;
    ara::Worker analysisWorker;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PlaybackRenderer)
};
}

#endif
