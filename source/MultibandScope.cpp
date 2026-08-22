#include "MultibandScope.h"

#include <algorithm>
#include <cmath>

namespace ana
{
MultibandScope::MultibandScope()
{
    reset();
}

void MultibandScope::prepare(const double newSampleRate)
{
    const auto validSampleRate = std::max(1.0, newSampleRate);
    displayDecimation = std::max(size_t { 1 }, static_cast<size_t>(std::ceil(validSampleRate / 48000.0)));
    displayDecimationCounter = 0;
    sampleRate.store(validSampleRate / static_cast<double>(displayDecimation), std::memory_order_release);
    crossover.prepare(validSampleRate);
    crossover.setActiveSplitCount(currentActiveSplitCount);
    crossover.setSplitFrequencies(currentFrequencies);
    reset();
}

void MultibandScope::reset()
{
    crossover.reset();

    for (auto& band : ringBuffers)
        for (auto& channel : band)
            for (auto& sample : channel)
                sample.store(0.0f, std::memory_order_relaxed);

    writeCursor.store(0, std::memory_order_release);
    displayDecimationCounter = 0;
}

void MultibandScope::setCrossoverSettings(const size_t activeSplitCount,
                                          const dsp::Crossover::SplitFrequencies& frequencies)
{
    const auto constrainedCount = std::min(activeSplitCount, dsp::Crossover::numSplits);

    if (frequencies != currentFrequencies)
    {
        currentFrequencies = frequencies;
        crossover.setSplitFrequencies(currentFrequencies);
    }

    if (constrainedCount != currentActiveSplitCount)
    {
        currentActiveSplitCount = constrainedCount;
        crossover.setActiveSplitCount(currentActiveSplitCount);
    }
}

void MultibandScope::processBlock(const juce::AudioBuffer<float>& buffer) noexcept
{
    if (buffer.getNumChannels() == 0 || buffer.getNumSamples() == 0)
        return;

    const auto* left = buffer.getReadPointer(0);
    const auto* right = buffer.getNumChannels() > 1 ? buffer.getReadPointer(1) : left;
    auto cursor = writeCursor.load(std::memory_order_relaxed);

    for (int sampleIndex = 0; sampleIndex < buffer.getNumSamples(); ++sampleIndex)
    {
        const auto ranges = crossover.processSample(static_cast<double>(left[sampleIndex]),
                                                    static_cast<double>(right[sampleIndex]));

        if (displayDecimationCounter != 0)
        {
            displayDecimationCounter = (displayDecimationCounter + 1) % displayDecimation;
            continue;
        }

        const auto ringIndex = static_cast<size_t>(cursor) & (ringCapacity - 1);

        for (size_t bandIndex = 0; bandIndex < numBands; ++bandIndex)
        {
            ringBuffers[bandIndex][0][ringIndex].store(
                static_cast<float>(ranges[bandIndex].left), std::memory_order_relaxed);
            ringBuffers[bandIndex][1][ringIndex].store(
                static_cast<float>(ranges[bandIndex].right), std::memory_order_relaxed);
        }

        ++cursor;
        displayDecimationCounter = (displayDecimationCounter + 1) % displayDecimation;
    }

    writeCursor.store(cursor, std::memory_order_release);
}

void MultibandScope::copySince(Snapshot& destination, uint64_t& readCursor) const
{
    const auto endCursor = writeCursor.load(std::memory_order_acquire);
    const auto earliestCursor = endCursor > ringCapacity ? endCursor - ringCapacity : 0;
    const auto startCursor = std::clamp(readCursor, earliestCursor, endCursor);
    const auto sampleCount = static_cast<size_t>(endCursor - startCursor);

    for (size_t bandIndex = 0; bandIndex < numBands; ++bandIndex)
    {
        for (size_t channelIndex = 0; channelIndex < numSourceChannels; ++channelIndex)
        {
            auto& channel = destination[bandIndex][channelIndex];
            channel.resize(sampleCount);

            for (size_t sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex)
            {
                const auto ringIndex = static_cast<size_t>(startCursor + sampleIndex) & (ringCapacity - 1);
                channel[sampleIndex] = ringBuffers[bandIndex][channelIndex][ringIndex]
                    .load(std::memory_order_relaxed);
            }
        }
    }

    readCursor = endCursor;
}

double MultibandScope::getSampleRate() const noexcept
{
    return sampleRate.load(std::memory_order_acquire);
}

uint64_t MultibandScope::getWriteCursor() const noexcept
{
    return writeCursor.load(std::memory_order_acquire);
}
} // namespace ana
