#include "Processor.h"
#include "shared/analyzer/DisplaySettings.h"
#include "shared/analyzer/SpectrumChannelLevels.h"

#include <algorithm>

namespace ana::spec
{
namespace
{
using ChannelValues = std::array<float, channelCount>;

ChannelValues decibelValues(const fft::StereoSpectrumLevels& levels) noexcept
{
    return { levels.stereoDecibels, levels.leftDecibels, levels.rightDecibels,
             levels.midDecibels, levels.sideDecibels, levels.deltaDecibels };
}

template <typename Channels>
void fillChannels(Channels& channels, const float value) noexcept
{
    for (auto& channel : channels)
        channel.fill(value);
}

template <typename AtomicChannels>
void storeChannels(AtomicChannels& channels, const float value) noexcept
{
    for (auto& channel : channels)
        for (auto& bin : channel)
            bin.store(value, std::memory_order_relaxed);
}
}

void SpecProcessor::resetRtState() noexcept
{
    fillChannels(averageLevels, minimumDecibels);
    storeChannels(publishedAverageLevels, minimumDecibels);
    storeChannels(publishedCurrentLevels, minimumDecibels);
}

void SpecProcessor::resetMapState() noexcept
{
    storeChannels(publishedMapCurrentLevels, minimumDecibels);
    fillChannels(mapAccumulatedLevels, minimumDecibels);
    mapAccumulatedFrameCount = 0;
    mapAccumulatorFftSize = 0;
    publishedMapFftSize.store(0, std::memory_order_release);
}

void SpecProcessor::requestRtReset() noexcept
{
    rtResetRequested.store(true, std::memory_order_release);
}

void SpecProcessor::setRtMapMode(const bool shouldUseMap) noexcept
{
    rtMapMode.store(shouldUseMap, std::memory_order_release);
}

void SpecProcessor::processBlock(const juce::AudioBuffer<float>& buffer,
                                 const int requestedFftSize,
                                 const float fftOverlap,
                                 const float averagingTimeMilliseconds,
                                 const int mapTimeOverlapChoice) noexcept
{
    if (buffer.getNumSamples() <= 0 || buffer.getNumChannels() <= 0)
        return;

    const auto clear = clearRequested.exchange(false, std::memory_order_acq_rel);
    const auto resetRt = rtResetRequested.exchange(false, std::memory_order_acq_rel);
    if (clear)
        reset();
    else if (resetRt)
    {
        fftStream.reset();
        mapFftStream.reset();
        resetRtState();
        resetMapState();
        revision.fetch_add(1, std::memory_order_release);
        mapRevision.fetch_add(1, std::memory_order_release);
    }

    const auto useMap = rtMapMode.load(std::memory_order_acquire);
    if (processingMapMode != useMap)
    {
        fftStream.reset();
        mapFftStream.reset();
        resetRtState();
        resetMapState();
        revision.fetch_add(1, std::memory_order_release);
        mapRevision.fetch_add(1, std::memory_order_release);
        processingMapMode = useMap;
        if (! useMap)
            processingMapTimeOverlapChoice = -1;
    }

    if (frozen.load(std::memory_order_acquire))
        return;

    if (useMap)
    {
        const auto overlapChoice = juce::jlimit(0, mapTimeOverlapChoiceCount - 1, mapTimeOverlapChoice);
        if (processingMapTimeOverlapChoice != overlapChoice)
        {
            mapFftStream.reset();
            resetMapState();
            processingMapTimeOverlapChoice = overlapChoice;
            mapRevision.fetch_add(1, std::memory_order_release);
        }

        mapOversamplingFactor = mapTimeOversamplingFactor(overlapChoice);
        const auto baseHop = mapBaseHopSizeForFftSize(requestedFftSize);
        const auto analysisHop = std::max(1, baseHop / mapOversamplingFactor);
        mapFftStream.processBlockWithHop(buffer, requestedFftSize, analysisHop,
                                         fft::WindowType::gaussian200Db,
                                         [this] (const fft::StereoFftFrame& frame)
                                         {
                                             publishMap(frame);
                                         });
        return;
    }

    fftStream.processBlock(buffer, requestedFftSize, fftOverlap,
                           [this, averagingTimeMilliseconds] (const fft::StereoFftFrame& frame)
                           {
                               publish(frame, averagingTimeMilliseconds);
                           });
}

void SpecProcessor::copyMapSpectrum(const Channel channel,
                                    std::vector<float>& destination,
                                    int& fftSize) const
{
    fftSize = publishedMapFftSize.load(std::memory_order_acquire);
    const auto binCount = fftSize > 0 ? fftSize / 2 + 1 : 0;
    destination.resize(static_cast<size_t>(binCount));

    const auto index = std::min(channelIndex(channel), channelCount - 1);
    const auto& source = publishedMapCurrentLevels[index];
    for (int bin = 0; bin < binCount; ++bin)
        destination[static_cast<size_t>(bin)] =
            source[static_cast<size_t>(bin)].load(std::memory_order_acquire);
}

uint64_t SpecProcessor::getMapRevision() const noexcept
{
    return mapRevision.load(std::memory_order_acquire);
}

void SpecProcessor::publish(const fft::StereoFftFrame& frame,
                            const float averagingTimeMilliseconds) noexcept
{
    const auto fftSize = frame.size;
    if (lastFrameSize != fftSize)
    {
        resetRtState();
        resetMaximums();
        lastFrameSize = fftSize;
    }

    const auto binCount = fftSize / 2 + 1;
    const auto averageAlpha = analyzer_display::averagingAlpha(
        frame.hopSize, fftStream.getSampleRate(), averagingTimeMilliseconds);
    const auto averageUpdate = 1.0f - averageAlpha;

    for (int bin = 0; bin < binCount; ++bin)
    {
        const auto binIndex = static_cast<size_t>(bin);
        const auto values = decibelValues(
            fft::calculateStereoSpectrumLevels(frame, bin, 2.0f, minimumDecibels));

        for (size_t channel = 0; channel < channelCount; ++channel)
        {
            auto& average = averageLevels[channel][binIndex];
            auto& maximum = maximumLevels[channel][binIndex];
            const auto current = values[channel];
            average = averageAlpha * average + averageUpdate * current;
            maximum = std::max(maximum, current);

            publishedCurrentLevels[channel][binIndex].store(current, std::memory_order_relaxed);
            publishedAverageLevels[channel][binIndex].store(average, std::memory_order_relaxed);
            publishedMaximumLevels[channel][binIndex].store(maximum, std::memory_order_relaxed);
        }
    }

    publishedFftSize.store(fftSize, std::memory_order_release);
    revision.fetch_add(1, std::memory_order_release);
}

void SpecProcessor::publishMap(const fft::StereoFftFrame& frame) noexcept
{
    const auto fftSize = frame.size;
    const auto binCount = fftSize / 2 + 1;
    if (mapAccumulatorFftSize != fftSize)
    {
        fillChannels(mapAccumulatedLevels, minimumDecibels);
        mapAccumulatedFrameCount = 0;
        mapAccumulatorFftSize = fftSize;
    }

    const auto firstSubframe = mapAccumulatedFrameCount == 0;
    for (int bin = 0; bin < binCount; ++bin)
    {
        const auto binIndex = static_cast<size_t>(bin);
        const auto values = decibelValues(
            fft::calculateStereoSpectrumLevels(frame, bin, 1.0f, minimumDecibels));

        for (size_t channel = 0; channel < channelCount; ++channel)
        {
            auto& accumulated = mapAccumulatedLevels[channel][binIndex];
            accumulated = firstSubframe ? values[channel]
                                        : std::max(accumulated, values[channel]);
        }
    }

    ++mapAccumulatedFrameCount;
    if (mapAccumulatedFrameCount < std::max(1, mapOversamplingFactor))
        return;

    for (int bin = 0; bin < binCount; ++bin)
    {
        const auto binIndex = static_cast<size_t>(bin);
        for (size_t channel = 0; channel < channelCount; ++channel)
            publishedMapCurrentLevels[channel][binIndex].store(
                mapAccumulatedLevels[channel][binIndex], std::memory_order_relaxed);
    }

    mapAccumulatedFrameCount = 0;
    publishedMapFftSize.store(fftSize, std::memory_order_release);
    mapRevision.fetch_add(1, std::memory_order_release);
}
}
