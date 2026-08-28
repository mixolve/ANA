#include "FrequencySpectrumProcessor.h"

#include <algorithm>
#include <cmath>

namespace ana::freq
{
namespace
{
constexpr float floorDecibels = -120.0f;

float toDecibels(const float magnitude, const int fftSize) noexcept
{
    return juce::jmax(floorDecibels,
                      juce::Decibels::gainToDecibels(magnitude * 2.0f / static_cast<float>(fftSize), floorDecibels));
}
}

FrequencySpectrumProcessor::FrequencySpectrumProcessor() { reset(); }

void FrequencySpectrumProcessor::prepare(const double newSampleRate)
{
    fftStream.prepare(newSampleRate);
    reset();
}

void FrequencySpectrumProcessor::reset()
{
    fftStream.reset();
    leftAverage.fill(floorDecibels);
    rightAverage.fill(floorDecibels);
    stereoAverage.fill(floorDecibels);
    leftMaximum.fill(floorDecibels);
    rightMaximum.fill(floorDecibels);
    stereoMaximum.fill(floorDecibels);
    midAverage.fill(floorDecibels);
    sideAverage.fill(floorDecibels);
    midMaximum.fill(floorDecibels);
    sideMaximum.fill(floorDecibels);

    for (auto* spectrum : { &publishedLeftAverage, &publishedRightAverage, &publishedStereoAverage,
                            &publishedMidAverage, &publishedSideAverage,
                            &publishedLeftMaximum, &publishedRightMaximum, &publishedStereoMaximum,
                            &publishedMidMaximum, &publishedSideMaximum })
        for (auto& value : *spectrum)
            value.store(floorDecibels, std::memory_order_relaxed);

    publishedFftSize.store(0, std::memory_order_release);
    lastFrameSize = 0;
    revision.fetch_add(1, std::memory_order_release);
}

void FrequencySpectrumProcessor::requestClear() noexcept
{
    clearRequested.store(true, std::memory_order_release);
}

void FrequencySpectrumProcessor::setFrozen(const bool shouldFreeze) noexcept
{
    frozen.store(shouldFreeze, std::memory_order_release);
}

bool FrequencySpectrumProcessor::isFrozen() const noexcept
{
    return frozen.load(std::memory_order_acquire);
}

void FrequencySpectrumProcessor::processBlock(const juce::AudioBuffer<float>& buffer,
                                               const int requestedBlockSize,
                                               const float overlap,
                                               const float averagingTimeMilliseconds) noexcept
{
    if (buffer.getNumSamples() <= 0 || buffer.getNumChannels() <= 0)
        return;

    if (clearRequested.exchange(false, std::memory_order_acq_rel))
        reset();

    if (frozen.load(std::memory_order_acquire))
        return;

    fftStream.processBlock(buffer, requestedBlockSize, overlap,
                           [this, averagingTimeMilliseconds] (const fft::StereoFftFrame& frame)
                           {
                               publish(frame, averagingTimeMilliseconds);
                           });
}

void FrequencySpectrumProcessor::copySpectrum(const Channel channel,
                                               const DisplayType type,
                                               std::vector<float>& destination,
                                               int& fftSize) const
{
    fftSize = publishedFftSize.load(std::memory_order_acquire);
    const auto binCount = fftSize > 0 ? fftSize / 2 + 1 : 0;
    destination.resize(static_cast<size_t>(binCount));

    const auto* source = [&] ()
    {
        if (type == DisplayType::realtimeAverage)
        {
            switch (channel)
            {
                case Channel::stereo: return &publishedStereoAverage;
                case Channel::left: return &publishedLeftAverage;
                case Channel::right: return &publishedRightAverage;
                case Channel::mid: return &publishedMidAverage;
                case Channel::side: return &publishedSideAverage;
            }
        }

        switch (channel)
        {
            case Channel::stereo: return &publishedStereoMaximum;
            case Channel::left: return &publishedLeftMaximum;
            case Channel::right: return &publishedRightMaximum;
            case Channel::mid: return &publishedMidMaximum;
            case Channel::side: return &publishedSideMaximum;
        }

        return &publishedLeftAverage;
    }();

    for (int index = 0; index < binCount; ++index)
        destination[static_cast<size_t>(index)] = (*source)[static_cast<size_t>(index)].load(std::memory_order_acquire);
}

double FrequencySpectrumProcessor::getSampleRate() const noexcept
{
    return fftStream.getSampleRate();
}

uint64_t FrequencySpectrumProcessor::getRevision() const noexcept
{
    return revision.load(std::memory_order_acquire);
}

void FrequencySpectrumProcessor::publish(const fft::StereoFftFrame& frame,
                                         const float averagingTimeMilliseconds) noexcept
{
    const auto fftSize = frame.size;
    if (lastFrameSize != fftSize)
    {
        leftAverage.fill(floorDecibels);
        rightAverage.fill(floorDecibels);
        stereoAverage.fill(floorDecibels);
        leftMaximum.fill(floorDecibels);
        rightMaximum.fill(floorDecibels);
        stereoMaximum.fill(floorDecibels);
        midAverage.fill(floorDecibels);
        sideAverage.fill(floorDecibels);
        midMaximum.fill(floorDecibels);
        sideMaximum.fill(floorDecibels);
        lastFrameSize = fftSize;
    }
    const auto binCount = fftSize / 2 + 1;
    const auto durationSeconds = static_cast<float>(fftSize) / static_cast<float>(fftStream.getSampleRate());
    const auto averagingSeconds = std::max(0.001f, averagingTimeMilliseconds * 0.001f);
    const auto averageAlpha = std::exp(-durationSeconds / averagingSeconds);

    for (int index = 0; index < binCount; ++index)
    {
        const auto complexValue = [] (const float* data, const int bin)
        {
            return juce::Point<float>(data[2 * bin], data[2 * bin + 1]);
        };
        const auto leftComplex = complexValue(frame.left, index);
        const auto rightComplex = complexValue(frame.right, index);
        const auto magnitude = [] (const juce::Point<float> value)
        {
            return std::hypot(value.x, value.y);
        };
        const auto left = toDecibels(magnitude(leftComplex), fftSize);
        const auto right = toDecibels(magnitude(rightComplex), fftSize);
        const auto stereo = toDecibels((magnitude(leftComplex) + magnitude(rightComplex)) * 0.5f, fftSize);
        const auto mid = toDecibels(magnitude((leftComplex + rightComplex) * 0.5f), fftSize);
        const auto side = toDecibels(magnitude((leftComplex - rightComplex) * 0.5f), fftSize);
        leftAverage[static_cast<size_t>(index)] = averageAlpha * leftAverage[static_cast<size_t>(index)]
            + (1.0f - averageAlpha) * left;
        rightAverage[static_cast<size_t>(index)] = averageAlpha * rightAverage[static_cast<size_t>(index)]
            + (1.0f - averageAlpha) * right;
        stereoAverage[static_cast<size_t>(index)] = averageAlpha * stereoAverage[static_cast<size_t>(index)]
            + (1.0f - averageAlpha) * stereo;
        leftMaximum[static_cast<size_t>(index)] = std::max(leftMaximum[static_cast<size_t>(index)], left);
        rightMaximum[static_cast<size_t>(index)] = std::max(rightMaximum[static_cast<size_t>(index)], right);
        stereoMaximum[static_cast<size_t>(index)] = std::max(stereoMaximum[static_cast<size_t>(index)], stereo);
        midAverage[static_cast<size_t>(index)] = averageAlpha * midAverage[static_cast<size_t>(index)]
            + (1.0f - averageAlpha) * mid;
        sideAverage[static_cast<size_t>(index)] = averageAlpha * sideAverage[static_cast<size_t>(index)]
            + (1.0f - averageAlpha) * side;
        midMaximum[static_cast<size_t>(index)] = std::max(midMaximum[static_cast<size_t>(index)], mid);
        sideMaximum[static_cast<size_t>(index)] = std::max(sideMaximum[static_cast<size_t>(index)], side);

        publishedLeftAverage[static_cast<size_t>(index)].store(leftAverage[static_cast<size_t>(index)], std::memory_order_relaxed);
        publishedRightAverage[static_cast<size_t>(index)].store(rightAverage[static_cast<size_t>(index)], std::memory_order_relaxed);
        publishedStereoAverage[static_cast<size_t>(index)].store(stereoAverage[static_cast<size_t>(index)], std::memory_order_relaxed);
        publishedLeftMaximum[static_cast<size_t>(index)].store(leftMaximum[static_cast<size_t>(index)], std::memory_order_relaxed);
        publishedRightMaximum[static_cast<size_t>(index)].store(rightMaximum[static_cast<size_t>(index)], std::memory_order_relaxed);
        publishedStereoMaximum[static_cast<size_t>(index)].store(stereoMaximum[static_cast<size_t>(index)], std::memory_order_relaxed);
        publishedMidAverage[static_cast<size_t>(index)].store(midAverage[static_cast<size_t>(index)], std::memory_order_relaxed);
        publishedSideAverage[static_cast<size_t>(index)].store(sideAverage[static_cast<size_t>(index)], std::memory_order_relaxed);
        publishedMidMaximum[static_cast<size_t>(index)].store(midMaximum[static_cast<size_t>(index)], std::memory_order_relaxed);
        publishedSideMaximum[static_cast<size_t>(index)].store(sideMaximum[static_cast<size_t>(index)], std::memory_order_relaxed);
    }

    publishedFftSize.store(fftSize, std::memory_order_release);
    revision.fetch_add(1, std::memory_order_release);
}
} // namespace ana::freq
