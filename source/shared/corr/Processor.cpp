#if ANA_VARIANT_RTM
#include "rtm/corr/Processor.h"
#else
#include "ara/corr/Processor.h"
#endif
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

template <typename AtomicModeBins>
void storeModeBins(AtomicModeBins& values, const float value) noexcept
{
    for (auto& mode : values)
        for (auto& bin : mode)
            bin.store(value, std::memory_order_relaxed);
}
}

CorrProcessor::CorrProcessor()
{
    reset();
}

CorrProcessor::Mode CorrProcessor::modeFromIndex(const int index) noexcept
{
    return static_cast<Mode>(juce::jlimit(0, modeCount - 1, index));
}

void CorrProcessor::prepare(const double newSampleRate) noexcept
{
    fftStream.prepare(newSampleRate);
    reset();
}

void CorrProcessor::reset() noexcept
{
    fftStream.reset();
   #if ANA_VARIANT_RTM
    fillModeBins(averages, 1.0f);
    fillModeBins(minimums, 1.0f);
    for (auto& state : rtmMinimumStates)
        resetMinimumWindowState(state);
   #else
    resetAraAccumulators();
   #endif
    storeModeBins(publishedAverages, 1.0f);
    storeModeBins(publishedMinimums, 1.0f);
    publishedFftSize.store(0, std::memory_order_release);
    lastFrameSize = 0;
    revision.fetch_add(1, std::memory_order_release);
}

void CorrProcessor::requestClear() noexcept
{
    clearRequested.store(true, std::memory_order_release);
}

void CorrProcessor::setFrozen(const bool shouldFreeze) noexcept
{
    frozen.store(shouldFreeze, std::memory_order_release);
}

bool CorrProcessor::isFrozen() const noexcept
{
    return frozen.load(std::memory_order_acquire);
}

void CorrProcessor::copyCorr(const Mode mode, const DisplayType type,
                             std::vector<float>& destination, int& fftSize) const
{
    fftSize = publishedFftSize.load(std::memory_order_acquire);
    const auto binCount = fftSize > 0 ? fftSize / 2 + 1 : 0;
    destination.resize(static_cast<size_t>(binCount));

    const auto index = modeArrayIndex(mode);
    const auto& source = type == DisplayType::average
        ? publishedAverages[index] : publishedMinimums[index];
    for (int bin = 0; bin < binCount; ++bin)
        destination[static_cast<size_t>(bin)] =
            source[static_cast<size_t>(bin)].load(std::memory_order_acquire);
}

double CorrProcessor::getSampleRate() const noexcept
{
    return fftStream.getSampleRate();
}

uint64_t CorrProcessor::getRevision() const noexcept
{
    return revision.load(std::memory_order_acquire);
}

void CorrProcessor::resetMinimumWindowState(MinimumWindowState& state) noexcept
{
    state.numeratorWindowSum.fill(0.0);
    state.denominatorWindowSum.fill(0.0);
    state.numeratorRing.clear();
    state.denominatorRing.clear();
    state.windowFrames = 0;
    state.ringPosition = 0;
    state.frameCount = 0;
    state.binCount = 0;
    state.hopSize = 0;
}

void CorrProcessor::updateMinimumWindow(
    const fft::StereoFftFrame& frame, const Mode mode,
    MinimumWindowState& state,
    std::array<float, maximumBinCount>& minimum,
    std::array<std::atomic<float>, maximumBinCount>& published) noexcept
{
    const auto fftSize = frame.size;
    const auto binCount = fftSize / 2 + 1;
    const auto windowFrames = std::max(1, (fftSize + frame.hopSize - 1) / frame.hopSize);

    if (state.windowFrames != windowFrames || state.binCount != binCount
        || state.hopSize != frame.hopSize)
    {
        resetMinimumWindowState(state);
        state.windowFrames = windowFrames;
        state.binCount = binCount;
        state.hopSize = frame.hopSize;
        const auto ringSize = static_cast<size_t>(windowFrames) * static_cast<size_t>(binCount);
        state.numeratorRing.assign(ringSize, 0.0);
        state.denominatorRing.assign(ringSize, 0.0);
    }

    const auto ringOffset = static_cast<size_t>(state.ringPosition)
        * static_cast<size_t>(binCount);
    const auto windowReady = state.frameCount + 1 >= state.windowFrames;

    for (int index = 0; index < binCount; ++index)
    {
        const auto arrayIndex = static_cast<size_t>(index);
        const auto ringIndex = ringOffset + arrayIndex;
        const auto left = complexBin(frame.left, index);
        const auto right = complexBin(frame.right, index);
        const auto leftPower = static_cast<double>(left.x) * left.x
            + static_cast<double>(left.y) * left.y;
        const auto rightPower = static_cast<double>(right.x) * right.x
            + static_cast<double>(right.y) * right.y;
        const auto leftMagnitude = std::sqrt(leftPower);
        const auto rightMagnitude = std::sqrt(rightPower);
        const auto crossReal = static_cast<double>(left.x) * right.x
            + static_cast<double>(left.y) * right.y;

        auto numerator = 0.0;
        auto denominator = 0.0;
        if (mode == Mode::phase)
        {
            const auto sharedEnergy = leftMagnitude * rightMagnitude;
            if (sharedEnergy > 1.0e-12)
            {
                numerator = juce::jlimit(static_cast<double>(minimumCoefficient),
                                         static_cast<double>(maximumCoefficient),
                                         crossReal / sharedEnergy);
                denominator = 1.0;
            }
        }
        else
        {
            const auto energy = leftPower + rightPower;
            if (energy > 1.0e-12)
            {
                numerator = mode == Mode::frequency
                    ? 2.0 * leftMagnitude * rightMagnitude
                    : 2.0 * crossReal;
                denominator = energy;
            }
        }

        state.numeratorWindowSum[arrayIndex] -= state.numeratorRing[ringIndex];
        state.denominatorWindowSum[arrayIndex] -= state.denominatorRing[ringIndex];
        state.numeratorRing[ringIndex] = numerator;
        state.denominatorRing[ringIndex] = denominator;
        state.numeratorWindowSum[arrayIndex] += numerator;
        state.denominatorWindowSum[arrayIndex] += denominator;

        if (! windowReady || state.denominatorWindowSum[arrayIndex] <= 1.0e-12)
            continue;

        const auto raw = state.numeratorWindowSum[arrayIndex]
            / state.denominatorWindowSum[arrayIndex];
        const auto local = static_cast<float>(mode == Mode::frequency
            ? juce::jlimit(static_cast<double>(frequencyModeMinimumCoefficient),
                           static_cast<double>(maximumCoefficient), raw)
            : juce::jlimit(static_cast<double>(minimumCoefficient),
                           static_cast<double>(maximumCoefficient), raw));

        minimum[arrayIndex] = std::min(minimum[arrayIndex], local);
        published[arrayIndex].store(minimum[arrayIndex], std::memory_order_relaxed);
    }

    state.ringPosition = (state.ringPosition + 1) % state.windowFrames;
    state.frameCount = std::min(state.frameCount + 1, state.windowFrames);
}
}
