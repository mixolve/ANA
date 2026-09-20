#include "PlaybackRenderer.h"
#include "AraAnalyzer.h"
#include "AraSourceCatalog.h"
#include "BandlimitedResampler.h"

#if JucePlugin_Enable_ARA

#include <algorithm>
#include <cmath>
#include <utility>

namespace ana::ara
{
class PlaybackRenderer::SharedReaderThread final : public juce::TimeSliceThread
{
public:
    SharedReaderThread()
        : juce::TimeSliceThread("ANA_ARA Reader")
    {
        startThread(juce::Thread::Priority::high);
    }
};

class PlaybackRenderer::AudioSourceReader
{
public:
    AudioSourceReader(juce::ARAAudioSource* source,
                      const bool shouldBuffer,
                      juce::TimeSliceThread& readerThread,
                      const int readAheadSize)
    {
        directReader = std::make_unique<juce::ARAAudioSourceReader>(source);

        if (shouldBuffer)
        {
            auto bufferingReader = std::make_unique<juce::BufferingAudioReader>(
                new juce::ARAAudioSourceReader(source), readerThread, readAheadSize);
            bufferingReader->setReadTimeout(0);
            bufferedReader = std::move(bufferingReader);
        }
    }

    bool read(juce::AudioBuffer<float>* const destination,
              const int destinationStartSample,
              const int numberOfSamples,
              const juce::int64 readerStartSample,
              const bool useLeftChannel,
              const bool useRightChannel)
    {
        if (bufferedReader != nullptr
            && bufferedReader->read(destination, destinationStartSample, numberOfSamples,
                                    readerStartSample, useLeftChannel, useRightChannel))
            return true;

        // Fall back to an independent reader until scheduled read-ahead is ready.
        return directReader != nullptr
            && directReader->read(destination, destinationStartSample, numberOfSamples,
                                  readerStartSample, useLeftChannel, useRightChannel);
    }

private:
    std::unique_ptr<juce::AudioFormatReader> directReader;
    std::unique_ptr<juce::AudioFormatReader> bufferedReader;
};

PlaybackRenderer::PlaybackRenderer(ARA::PlugIn::DocumentController* documentController,
                                   ProcessingLock& processingLock)
    : juce::ARAPlaybackRenderer(documentController),
      lock(processingLock),
      araDocumentController(documentController),
      analysisWorker("ANA_ARA ARA Analysis",
                     [this] (const ara::AnalysisRequest& request,
                             const ara::Worker::ShouldCancel& shouldCancel,
                             const ara::Worker::ProgressCallback& onProgress)
                     {
                         return buildAraAnalysisResult(request, shouldCancel, onProgress);
                     })
{
}

PlaybackRenderer::~PlaybackRenderer()
{
    analysisWorker.stop();
}

void PlaybackRenderer::prepareToPlay(const double sampleRate,
                                     const int maximumSamplesPerBlock,
                                     const int numChannels,
                                     juce::AudioProcessor::ProcessingPrecision,
                                     const AlwaysNonRealtime alwaysNonRt)
{
    BandlimitedResampler::prepare();
    preparedSampleRate = sampleRate;
    preparedBlockSize = maximumSamplesPerBlock;
    preparedChannelCount = numChannels;
    useBufferedReaders = alwaysNonRt == AlwaysNonRealtime::no;
    renderedBuffer = std::make_unique<juce::AudioBuffer<float>>(preparedChannelCount,
                                                                preparedBlockSize);
    mixBuffer = std::make_unique<juce::AudioBuffer<float>>(preparedChannelCount, preparedBlockSize);
    isPrepared = true;
    rebuildReaders();
    analysisWorker.rescheduleLatest(regionGeneration.load(std::memory_order_relaxed));
}

void PlaybackRenderer::rebuildReaders()
{
    readers.clear();
    sourceBufferSize = preparedBlockSize + BandlimitedResampler::kernelRadius * 2 + 4;

    const auto addReader = [this] (juce::ARAAudioSource* audioSource)
    {
        if (audioSource == nullptr || readers.find(audioSource) != readers.end())
            return;

        const auto readAheadSize = std::max(4 * preparedBlockSize,
                                            juce::roundToInt(2.0 * preparedSampleRate));
        readers.emplace(audioSource,
                        std::make_unique<AudioSourceReader>(audioSource,
                                                           useBufferedReaders,
                                                           *sharedReaderThread,
                                                           readAheadSize));
    };

    if (araDocumentController != nullptr)
        if (auto* document = araDocumentController->getDocument())
            for (auto* audioSource : document->getAudioSources<juce::ARAAudioSource>())
                addReader(audioSource);

    for (auto* playbackRegion : getPlaybackRegions())
    {
        if (playbackRegion == nullptr)
            continue;

        auto* modification = playbackRegion->getAudioModification();
        auto* audioSource = modification != nullptr ? modification->getAudioSource() : nullptr;
        if (audioSource == nullptr)
            continue;

        addReader(audioSource);

        const auto playbackDuration = playbackRegion->getDurationInPlaybackTime();
        const auto modificationDuration = playbackRegion->getDurationInAudioModificationTime();

        if (playbackDuration > 0.0 && preparedSampleRate > 0.0)
        {
            const auto sourceIncrement = audioSource->getSampleRate() / preparedSampleRate
                * modificationDuration / playbackDuration;
            sourceBufferSize = std::max(sourceBufferSize,
                juce::roundToInt(std::ceil(static_cast<double>(preparedBlockSize)
                                           * std::max(1.0, sourceIncrement)))
                    + BandlimitedResampler::kernelRadius * 2 + 4);
        }
    }

    sourceBuffer = std::make_unique<juce::AudioBuffer<float>>(preparedChannelCount, sourceBufferSize);
}

bool PlaybackRenderer::setRtSourceChoices(const std::vector<ara::SourceChoice>& choices)
{
    auto activeTakes = std::make_shared<std::vector<RtTake>>();
    activeTakes->reserve(choices.size());
    size_t activeChoiceCount = 0;
    for (const auto& choice : choices)
    {
        if (! choice.activeTake || choice.audioSourcePersistentId.isEmpty()
            || choice.playbackDurationSeconds <= 0.0 || choice.playRate <= 0.0)
            continue;

        ++activeChoiceCount;
        auto* audioSource = findHostTakeAudioSource(araDocumentController, choice);
        if (audioSource == nullptr)
            continue;

        activeTakes->push_back({ audioSource,
                                choice.playbackStartSeconds,
                                choice.playbackDurationSeconds,
                                choice.sourceStartSeconds,
                                choice.playRate,
                                choice.gain });
    }
    const auto resolved = activeTakes->size() == activeChoiceCount;
    if (! resolved)
        activeTakes->clear();

    hostTakeSelectionPresent.store(resolved && activeChoiceCount > 0,
                                   std::memory_order_release);
    std::shared_ptr<const std::vector<RtTake>> publishedTakes = std::move(activeTakes);
    std::atomic_store_explicit(&rtTakes, std::move(publishedTakes), std::memory_order_release);
    return resolved;
}

void PlaybackRenderer::releaseResources()
{
    isPrepared = false;
    readers.clear();
    renderedBuffer.reset();
    mixBuffer.reset();
    sourceBuffer.reset();
}

bool PlaybackRenderer::processBlock(juce::AudioBuffer<float>& buffer,
                                    const juce::AudioProcessor::Realtime,
                                    const juce::AudioPlayHead::PositionInfo& positionInfo) noexcept
{
    const auto processingLock = lock.tryProcessingReadLock();

    if (! processingLock.isLocked())
        return false;

    const auto numSamples = buffer.getNumSamples();
    const auto timeInSamples = positionInfo.getTimeInSamples().orFallback(0);
    auto success = true;

    jassert(numSamples <= preparedBlockSize);
    jassert(buffer.getNumChannels() == preparedChannelCount);

    if (numSamples > preparedBlockSize
        || buffer.getNumChannels() != preparedChannelCount
        || renderedBuffer == nullptr
        || mixBuffer == nullptr
        || sourceBuffer == nullptr)
        return false;

    renderedBuffer->clear();

    if (positionInfo.getIsPlaying())
    {
        const auto blockRange = juce::Range<juce::int64>::withStartAndLength(timeInSamples, numSamples);
        const auto renderSource = [&] (juce::ARAAudioSource* audioSource,
                                       const juce::Range<juce::int64> renderRange,
                                       const double sourceStart,
                                       const double sourceIncrement,
                                       const float gain)
        {
            const auto reader = readers.find(audioSource);
            if (reader == readers.end())
                return false;

            const auto samplesToRead = static_cast<int>(renderRange.getLength());
            const auto startInBuffer = static_cast<int>(renderRange.getStart() - blockRange.getStart());
            mixBuffer->clear();

            if (std::abs(sourceIncrement - 1.0) < 1.0e-9
                && std::abs(sourceStart - std::round(sourceStart)) < 1.0e-6)
            {
                if (! reader->second->read(mixBuffer.get(),
                                           startInBuffer,
                                           samplesToRead,
                                           static_cast<juce::int64>(std::llround(sourceStart)),
                                           true,
                                           true))
                    return false;
            }
            else
            {
                const auto firstSourceSample = static_cast<juce::int64>(std::floor(sourceStart))
                    - BandlimitedResampler::kernelRadius + 1;
                const auto lastSourcePosition = sourceStart
                    + static_cast<double>(std::max(0, samplesToRead - 1)) * sourceIncrement;
                const auto sourceReadEnd = static_cast<juce::int64>(std::floor(lastSourcePosition))
                    + BandlimitedResampler::kernelRadius + 1;
                const auto sourceReadCount = static_cast<int>(sourceReadEnd - firstSourceSample);

                if (sourceReadCount <= 0 || sourceReadCount > sourceBufferSize)
                    return false;

                sourceBuffer->clear();
                const auto readableStart = std::max<juce::int64>(0, firstSourceSample);
                const auto readableEnd = std::min<juce::int64>(audioSource->getSampleCount(),
                                                                sourceReadEnd);
                const auto destinationOffset = static_cast<int>(readableStart - firstSourceSample);
                const auto readableCount = static_cast<int>(std::max<juce::int64>(
                    0, readableEnd - readableStart));
                if (readableCount > 0
                    && ! reader->second->read(sourceBuffer.get(),
                                              destinationOffset,
                                              readableCount,
                                              readableStart,
                                              true,
                                              true))
                    return false;

                for (int sampleIndex = 0; sampleIndex < samplesToRead; ++sampleIndex)
                {
                    const auto sourcePosition = sourceStart
                        - static_cast<double>(firstSourceSample)
                        + static_cast<double>(sampleIndex) * sourceIncrement;

                    for (int channel = 0; channel < preparedChannelCount; ++channel)
                        mixBuffer->setSample(channel,
                                             startInBuffer + sampleIndex,
                                             BandlimitedResampler::interpolate(
                                                 sourceBuffer->getReadPointer(channel),
                                                 sourceReadCount,
                                                 sourcePosition,
                                                 sourceIncrement));
                }
            }

            for (int channel = 0; channel < preparedChannelCount; ++channel)
                renderedBuffer->addFrom(channel,
                                        startInBuffer,
                                        *mixBuffer,
                                        channel,
                                        startInBuffer,
                                        samplesToRead,
                                        gain);
            return true;
        };

        const auto activeTakes = std::atomic_load_explicit(&rtTakes, std::memory_order_acquire);
        const auto hasHostTakeMap = activeTakes != nullptr && ! activeTakes->empty();
        const auto hasHostTakeSelection = hostTakeSelectionPresent.load(std::memory_order_acquire);
        if (hasHostTakeMap)
        {
            for (const auto& take : *activeTakes)
            {
                const auto playbackStart = static_cast<juce::int64>(std::llround(
                    take.playbackStartSeconds * preparedSampleRate));
                const auto playbackLength = static_cast<juce::int64>(std::llround(
                    take.playbackDurationSeconds * preparedSampleRate));
                const auto renderRange = blockRange.getIntersectionWith(
                    juce::Range<juce::int64>::withStartAndLength(playbackStart, playbackLength));
                if (renderRange.isEmpty())
                    continue;

                if (readers.find(take.audioSource) == readers.end())
                {
                    success = false;
                    continue;
                }

                auto* audioSource = take.audioSource;
                const auto sourceRate = audioSource->getSampleRate();
                const auto renderStartTime = static_cast<double>(renderRange.getStart()) / preparedSampleRate;
                const auto sourceStart = (take.sourceStartSeconds
                    + (renderStartTime - take.playbackStartSeconds) * take.playRate) * sourceRate;
                const auto sourceIncrement = sourceRate / preparedSampleRate * take.playRate;
                if (! renderSource(audioSource, renderRange, sourceStart, sourceIncrement,
                                   static_cast<float>(take.gain)))
                    success = false;
            }
        }

        if (! hasHostTakeMap && ! hasHostTakeSelection)
        {
            for (auto* playbackRegion : getPlaybackRegions())
            {
                if (playbackRegion == nullptr)
                {
                    success = false;
                    continue;
                }

                const auto playbackRange = playbackRegion->getSampleRange(
                    preparedSampleRate, juce::ARAPlaybackRegion::IncludeHeadAndTail::no);
                const auto renderRange = blockRange.getIntersectionWith(playbackRange);
                if (renderRange.isEmpty())
                    continue;

                auto* modification = playbackRegion->getAudioModification();
                auto* audioSource = modification != nullptr ? modification->getAudioSource() : nullptr;
                const auto playbackDuration = playbackRegion->getDurationInPlaybackTime();
                if (audioSource == nullptr || playbackDuration <= 0.0)
                {
                    success = false;
                    continue;
                }

                const auto modificationDuration = playbackRegion->getDurationInAudioModificationTime();
                const auto sourceRate = audioSource->getSampleRate();
                const auto sourceIncrement = sourceRate / preparedSampleRate
                    * modificationDuration / playbackDuration;
                const auto renderStartTime = static_cast<double>(renderRange.getStart()) / preparedSampleRate;
                const auto sourceStart = (playbackRegion->getStartInAudioModificationTime()
                    + (renderStartTime - playbackRegion->getStartInPlaybackTime())
                        * modificationDuration / playbackDuration) * sourceRate;
                if (! renderSource(audioSource, renderRange, sourceStart, sourceIncrement, 1.0f))
                    success = false;
            }
        }
    }

    if (success)
        for (int channel = 0; channel < preparedChannelCount; ++channel)
            buffer.copyFrom(channel, 0, *renderedBuffer, channel, 0, numSamples);

    return success;
}

void PlaybackRenderer::didAddPlaybackRegion(ARA::PlugIn::PlaybackRegion*) noexcept
{
    playbackRegionsChanged();
}

void PlaybackRenderer::didRemovePlaybackRegion(ARA::PlugIn::PlaybackRegion*) noexcept
{
    playbackRegionsChanged();
}

void PlaybackRenderer::playbackRegionsChanged() noexcept
{
    regionGeneration.fetch_add(1, std::memory_order_relaxed);

    if (isPrepared)
    {
        try
        {
            rebuildReaders();
        }
        catch (...)
        {
            readers.clear();
        }
    }

    analysisWorker.rescheduleLatest(regionGeneration.load(std::memory_order_relaxed));
}

std::vector<juce::ARAPlaybackRegion*> PlaybackRenderer::collectPlaybackRegions() const
{
    std::vector<juce::ARAPlaybackRegion*> rendererRegions;
    rendererRegions.reserve(getPlaybackRegions().size());
    for (auto* playbackRegion : getPlaybackRegions())
        if (playbackRegion != nullptr)
            rendererRegions.push_back(playbackRegion);

    return collectAnalysisRegions(araDocumentController, rendererRegions);
}

void PlaybackRenderer::requestAraAnalysis(ara::AnalysisRequest request,
                                              const bool forceRefresh)
{
    request.regionGeneration = regionGeneration.load(std::memory_order_relaxed);
    analysisWorker.request(std::move(request), forceRefresh);
}

std::shared_ptr<const ara::AnalysisResult> PlaybackRenderer::getAraAnalysisResult() const
{
    return analysisWorker.getAnalysisResult();
}

std::vector<ara::SourceChoice> PlaybackRenderer::getAraSourceChoices() const
{
    return sourceChoiceCache.get(lock, [this]
    {
        return makeSourceChoices(collectPlaybackRegions());
    });
}

std::shared_ptr<ara::AnalysisResult> PlaybackRenderer::buildAraAnalysisResult(
    const ara::AnalysisRequest& request,
    const ara::Worker::ShouldCancel& shouldCancel,
    const ara::Worker::ProgressCallback& onProgress)
{
    const juce::ScopedReadLock processingLock(lock.getProcessingReadWriteLock());
    return analysePlaybackRegions(
        araDocumentController, collectPlaybackRegions(), request, request.revision,
        shouldCancel, onProgress);
}
}

#endif
