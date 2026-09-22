#include "Processor.h"
#include "shared/corr/Settings.h"
#include "shared/analyzer/DisplaySettings.h"

#include <algorithm>
#include <cmath>

namespace ana::corr
{
namespace
{
juce::Point<float> complexBin(const float* data, const int bin) noexcept
{
    return { data[2 * bin], data[2 * bin + 1] };
}

size_t modeArrayIndex(const CorrProcessor::Mode mode) noexcept
{
    return static_cast<size_t>(CorrProcessor::modeIndex(mode));
}

float correlationValue(const juce::Point<float> left,
                       const juce::Point<float> right,
                       const CorrProcessor::Mode mode) noexcept
{
    const auto leftMagnitude = std::hypot(left.x, left.y);
    const auto rightMagnitude = std::hypot(right.x, right.y);
    const auto crossReal = left.x * right.x + left.y * right.y;

    if (mode == CorrProcessor::Mode::phase)
    {
        const auto denominator = leftMagnitude * rightMagnitude;
        return denominator > 1.0e-12f
            ? juce::jlimit(minimumCoefficient, maximumCoefficient, crossReal / denominator)
            : 1.0f;
    }

    const auto energy = leftMagnitude * leftMagnitude + rightMagnitude * rightMagnitude;
    if (energy <= 1.0e-12f)
        return 1.0f;

    return mode == CorrProcessor::Mode::frequency
        ? juce::jlimit(frequencyModeMinimumCoefficient, maximumCoefficient,
                       2.0f * leftMagnitude * rightMagnitude / energy)
        : juce::jlimit(minimumCoefficient, maximumCoefficient, 2.0f * crossReal / energy);
}

template <typename ModeBins>
void fillModeBins(ModeBins& values, const float value) noexcept
{
    for (auto& mode : values)
        mode.fill(value);
}
}

void CorrProcessor::processBlock(const juce::AudioBuffer<float>& buffer, const int fftSize,
                                 const float fftOverlap, const float averagingTimeMilliseconds,
                                 const Mode mode) noexcept
{
    if (clearRequested.exchange(false, std::memory_order_acq_rel))
        reset();

    if (frozen.load(std::memory_order_acquire))
        return;

    fftStream.processBlock(buffer, fftSize, fftOverlap,
                           [this, averagingTimeMilliseconds, mode] (const fft::StereoFftFrame& frame)
                           {
                               publish(frame, averagingTimeMilliseconds, mode);
                           });
}

void CorrProcessor::publish(const fft::StereoFftFrame& frame,
                            const float averagingTimeMilliseconds,
                            const Mode mode) noexcept
{
    const auto fftSize = frame.size;
    if (lastFrameSize != fftSize)
    {
        fillModeBins(averages, 1.0f);
        fillModeBins(minimums, 1.0f);
        lastFrameSize = fftSize;
    }

    const auto binCount = fftSize / 2 + 1;
    const auto alpha = analyzer_display::averagingAlpha(
        frame.hopSize, fftStream.getSampleRate(), averagingTimeMilliseconds);
    const auto update = 1.0f - alpha;
    const auto modeIndexValue = modeArrayIndex(mode);

    for (int bin = 0; bin < binCount; ++bin)
    {
        const auto binIndex = static_cast<size_t>(bin);
        const auto current = correlationValue(
            complexBin(frame.left, bin), complexBin(frame.right, bin), mode);
        auto& average = averages[modeIndexValue][binIndex];
        average = alpha * average + update * current;
        publishedAverages[modeIndexValue][binIndex].store(average, std::memory_order_relaxed);

        // MIN holds the lowest AVG value, so AVG TIME filters the signal before
        // it participates in the historical minimum.
        auto& minimum = minimums[modeIndexValue][binIndex];
        minimum = std::min(minimum, average);
        publishedMinimums[modeIndexValue][binIndex].store(minimum, std::memory_order_relaxed);
    }

    publishedFftSize.store(fftSize, std::memory_order_release);
    revision.fetch_add(1, std::memory_order_release);
}
}
