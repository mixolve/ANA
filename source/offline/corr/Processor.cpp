#include "shared/corr/Processor.h"
#include "shared/corr/Settings.h"

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

template <typename ModeBins>
void fillModeBins(ModeBins& values, const float value) noexcept
{
    for (auto& mode : values)
        mode.fill(value);
}
}

void CorrProcessor::processOfflineBlock(const juce::AudioBuffer<float>& buffer, const int fftSize,
                                        const float fftOverlap, const Mode mode) noexcept
{
    if (clearRequested.exchange(false, std::memory_order_acq_rel))
        reset();

    fftStream.processBlock(buffer, fftSize, fftOverlap,
                           [this, mode] (const fft::StereoFftFrame& frame)
                           {
                               publishOffline(frame, mode);
                           });
}

void CorrProcessor::beginOfflineMinimumPass() noexcept
{
    // Restart only FFT framing for pass two; retain whole-file AVG from pass one.
    fftStream.reset();
    offlineMinimumPass = true;
    resetMinimumWindowState(offlineMinimumState);

    for (size_t mode = 0; mode < static_cast<size_t>(modeCount); ++mode)
    {
        for (size_t bin = 0; bin < offlineMinimums[mode].size(); ++bin)
        {
            // Seed MIN from final AVG so silent/degenerate bins cannot exceed it.
            offlineMinimums[mode][bin] =
                publishedAverages[mode][bin].load(std::memory_order_relaxed);
            publishedMinimums[mode][bin].store(
                offlineMinimums[mode][bin], std::memory_order_relaxed);
        }
    }
}

void CorrProcessor::resetOfflineAccumulators() noexcept
{
    for (auto& mode : offlineSums)
        mode.fill(0.0);
    for (auto& mode : offlineCounts)
        mode.fill(0);
    fillModeBins(offlineMinimums, 1.0f);
    resetMinimumWindowState(offlineMinimumState);
    offlineMinimumPass = false;
}

void CorrProcessor::publishOffline(const fft::StereoFftFrame& frame, const Mode mode) noexcept
{
    if (offlineMinimumPass)
    {
        publishOfflineMinimum(frame, mode);
        return;
    }

    const auto fftSize = frame.size;
    if (lastFrameSize != fftSize)
    {
        resetOfflineAccumulators();
        lastFrameSize = fftSize;
    }

    constexpr auto phaseMode = static_cast<size_t>(modeIndex(Mode::phase));
    constexpr auto frequencyMode = static_cast<size_t>(modeIndex(Mode::frequency));
    constexpr auto signedMode = static_cast<size_t>(modeIndex(Mode::signedCorrelation));
    const auto binCount = fftSize / 2 + 1;

    // Accumulate each mode independently; SIGNED cannot be reconstructed from averaged PHASE/FREQ.
    for (int bin = 0; bin < binCount; ++bin)
    {
        const auto binIndex = static_cast<size_t>(bin);
        const auto left = complexBin(frame.left, bin);
        const auto right = complexBin(frame.right, bin);
        const auto leftPower = static_cast<double>(left.x) * left.x
            + static_cast<double>(left.y) * left.y;
        const auto rightPower = static_cast<double>(right.x) * right.x
            + static_cast<double>(right.y) * right.y;
        const auto energy = leftPower + rightPower;
        if (energy <= 1.0e-12)
            continue;

        const auto leftMagnitude = std::sqrt(leftPower);
        const auto rightMagnitude = std::sqrt(rightPower);
        const auto sharedEnergy = leftMagnitude * rightMagnitude;
        const auto crossReal = static_cast<double>(left.x) * right.x
            + static_cast<double>(left.y) * right.y;

        if (sharedEnergy > 1.0e-12)
        {
            offlineSums[phaseMode][binIndex] +=
                juce::jlimit(static_cast<double>(minimumCoefficient), static_cast<double>(maximumCoefficient), crossReal / sharedEnergy);
            ++offlineCounts[phaseMode][binIndex];

            // SIGNED uses PHASE's bilateral-signal gate to reject one-sided near-silence.
            offlineSums[signedMode][binIndex] +=
                juce::jlimit(static_cast<double>(minimumCoefficient), static_cast<double>(maximumCoefficient), 2.0 * crossReal / energy);
            ++offlineCounts[signedMode][binIndex];
        }

        offlineSums[frequencyMode][binIndex] += juce::jlimit(
            0.0, 1.0, 2.0 * leftMagnitude * rightMagnitude / energy);
        ++offlineCounts[frequencyMode][binIndex];

        for (size_t modeIndexValue = 0; modeIndexValue < static_cast<size_t>(modeCount);
             ++modeIndexValue)
        {
            const auto count = offlineCounts[modeIndexValue][binIndex];
            const auto average = count > 0
                ? static_cast<float>(offlineSums[modeIndexValue][binIndex]
                    / static_cast<double>(count))
                : 1.0f;
            publishedAverages[modeIndexValue][binIndex].store(
                average, std::memory_order_relaxed);
        }
    }

    publishedFftSize.store(fftSize, std::memory_order_release);
    revision.fetch_add(1, std::memory_order_release);
}

void CorrProcessor::publishOfflineMinimum(const fft::StereoFftFrame& frame,
                                          const Mode mode) noexcept
{
    const auto index = modeArrayIndex(mode);
    updateMinimumWindow(frame, mode, offlineMinimumState,
                        offlineMinimums[index], publishedMinimums[index]);

    publishedFftSize.store(frame.size, std::memory_order_release);
    revision.fetch_add(1, std::memory_order_release);
}
}
