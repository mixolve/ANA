#include "shared/spec/Processor.h"
#include "shared/spec/SpectrumChannelLevels.h"

#include <algorithm>
#include <cmath>

namespace ana::spec
{
namespace
{
float powerToDecibels(const double power) noexcept
{
    return fft::spectrumGainToDecibels(
        static_cast<float>(std::sqrt(std::max(0.0, power))), SpecProcessor::minimumDecibels);
}

using ChannelValues = std::array<float, channelCount>;

ChannelValues decibelValues(const fft::StereoSpectrumLevels& levels) noexcept
{
    return { levels.stereoDecibels, levels.leftDecibels, levels.rightDecibels,
             levels.midDecibels, levels.sideDecibels };
}

ChannelValues gainValues(const fft::StereoSpectrumLevels& levels) noexcept
{
    return { levels.stereoGain, levels.leftGain, levels.rightGain,
             levels.midGain, levels.sideGain };
}
}

void SpecProcessor::resetOfflineStream() noexcept
{
    fftStream.reset();
    mapFftStream.reset();
}

void SpecProcessor::processOfflineBlock(const juce::AudioBuffer<float>& buffer,
                                        const int requestedFftSize,
                                        const float fftOverlap) noexcept
{
    if (buffer.getNumSamples() <= 0 || buffer.getNumChannels() <= 0)
        return;

    if (clearRequested.exchange(false, std::memory_order_acq_rel))
        reset();

    // FREQ uses Hann, matching REALTIME; MAP keeps its independent Gaussian STFT.
    fftStream.processBlock(buffer, requestedFftSize, fftOverlap,
                           [this] (const fft::StereoFftFrame& frame)
                           {
                               publishOffline(frame);
                           });
}

void SpecProcessor::processOfflineMapBlock(const juce::AudioBuffer<float>& buffer,
                                           const int requestedFftSize,
                                           const int hopSize,
                                           const OfflineFrameCallback& onFrame) noexcept
{
    if (buffer.getNumSamples() <= 0 || buffer.getNumChannels() <= 0 || ! onFrame)
        return;

    // MAP hop comes from the display raster; NONE is 1x raster sampling, not hop == FFT size.
    mapFftStream.processSpectrogramBlockWithHop(
        buffer, requestedFftSize, hopSize, onFrame);
}

void SpecProcessor::resetOfflineAccumulators() noexcept
{
    for (auto& channel : offlinePowerSums)
        channel.fill(0.0);
    offlineMagnitudeCount.fill(0);
}

void SpecProcessor::publishOffline(const fft::StereoFftFrame& frame) noexcept
{
    const auto fftSize = frame.size;
    if (lastFrameSize != fftSize)
    {
        resetOfflineAccumulators();
        resetMaximums();
        lastFrameSize = fftSize;
    }

    const auto binCount = fftSize / 2 + 1;
    for (int bin = 0; bin < binCount; ++bin)
    {
        const auto binIndex = static_cast<size_t>(bin);
        const auto levels = fft::calculateStereoSpectrumLevels(frame, bin, 2.0f, minimumDecibels);
        const auto gains = gainValues(levels);
        const auto decibels = decibelValues(levels);
        const auto count = ++offlineMagnitudeCount[binIndex];
        const auto inverseCount = 1.0 / static_cast<double>(count);

        for (size_t channel = 0; channel < channelCount; ++channel)
        {
            const auto gain = static_cast<double>(gains[channel]);
            auto& powerSum = offlinePowerSums[channel][binIndex];
            auto& maximum = maximumLevels[channel][binIndex];
            const auto current = decibels[channel];

            powerSum += gain * gain;
            maximum = std::max(maximum, current);
            publishedCurrentLevels[channel][binIndex].store(current, std::memory_order_relaxed);
            publishedAverageLevels[channel][binIndex].store(
                powerToDecibels(powerSum * inverseCount), std::memory_order_relaxed);
            publishedMaximumLevels[channel][binIndex].store(maximum, std::memory_order_relaxed);
        }
    }

    publishedFftSize.store(fftSize, std::memory_order_release);
    revision.fetch_add(1, std::memory_order_release);
}
}
