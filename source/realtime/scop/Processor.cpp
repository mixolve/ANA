#include "Processor.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ana
{
MultibandScop::MultibandScop()
{
    reset();
}

void MultibandScop::prepare(const double newSampleRate)
{
    const auto validSampleRate = std::max(1.0, newSampleRate);
    // Do not sample-drop the realtime waveform: offline accumulates every
    // source sample into its pixel envelope, at every host sample rate.
    sampleRate.store(validSampleRate, std::memory_order_release);
    crossover.prepare(validSampleRate);
    slopeCompensator.prepare(validSampleRate, true);
    crossover.setCrossoverCount(currentCrossoverCount);
    crossover.setCrossoverFrequencies(currentFrequencies);
    reset();
}

void MultibandScop::reset()
{
    crossover.reset();
    slopeCompensator.reset();

    // Ring contents are valid only below the published write cursors. Resetting
    // those cursors invalidates all previous entries without clearing ~50 MB
    // of atomic samples on the host's realtime audio thread. The GUI detects
    // resetRevision and drops its previous read positions.

    pendingPeakMinimums.fill(std::numeric_limits<float>::max());
    pendingPeakMaximums.fill(std::numeric_limits<float>::lowest());
    pendingPeakCount = 0;
    peakAccumulatorSlopeRevision = slopeCompensator.getAppliedKernelRevision();
    peakWriteCursor.store(0, std::memory_order_release);
    writeCursor.store(0, std::memory_order_release);
    resetRevision.fetch_add(1, std::memory_order_release);
}

void MultibandScop::setCrossoverSettings(const size_t crossoverCount,
                                          const dsp::LinkwitzRileyCrossover::CrossoverFrequencies& frequencies)
{
    const auto constrainedCount = std::min(crossoverCount, dsp::LinkwitzRileyCrossover::numCrossovers);

    if (frequencies != currentFrequencies)
    {
        currentFrequencies = frequencies;
        crossover.setCrossoverFrequencies(currentFrequencies);
    }

    if (constrainedCount != currentCrossoverCount)
    {
        currentCrossoverCount = constrainedCount;
        crossover.setCrossoverCount(currentCrossoverCount);
    }
}

void MultibandScop::setSlope(const float decibelsPerOctave) noexcept
{
    slopeCompensator.setSlope(decibelsPerOctave);
}

void MultibandScop::processBlock(const juce::AudioBuffer<float>& buffer) noexcept
{
    if (buffer.getNumChannels() == 0 || buffer.getNumSamples() == 0)
        return;

    const auto* left = buffer.getReadPointer(0);
    const auto* right = buffer.getNumChannels() > 1 ? buffer.getReadPointer(1) : left;
    auto cursor = writeCursor.load(std::memory_order_relaxed);

    for (int sampleIndex = 0; sampleIndex < buffer.getNumSamples(); ++sampleIndex)
    {
        // A newly installed FIR changes the meaning of the extrema. Do not
        // combine samples filtered with different SLOPE kernels in one bucket.
        const auto slopeRevision = slopeCompensator.getAppliedKernelRevision();
        if (peakAccumulatorSlopeRevision != slopeRevision)
        {
            pendingPeakMinimums.fill(std::numeric_limits<float>::max());
            pendingPeakMaximums.fill(std::numeric_limits<float>::lowest());
            pendingPeakCount = 0;
            peakAccumulatorSlopeRevision = slopeRevision;
        }

        const auto tilted = slopeCompensator.processSample(
            static_cast<double>(left[sampleIndex]), static_cast<double>(right[sampleIndex]));
        const auto bands = crossover.processSample(tilted.left, tilted.right);

        // Match the offline envelope calculation exactly: retain the actual
        // extrema of each native audio sample.  Decimating L/R loses peaks,
        // and decimation groups can straddle pixel-column boundaries.
        const auto modes = scop::makeAnalysisChannelSamples(
            static_cast<float>(tilted.left), static_cast<float>(tilted.right));
        const auto ringIndex = static_cast<size_t>(cursor) & (ringCapacity - 1);
        for (size_t modeIndex = 0; modeIndex < modes.size(); ++modeIndex)
        {
            pendingPeakMinimums[modeIndex] = std::min(pendingPeakMinimums[modeIndex], modes[modeIndex]);
            pendingPeakMaximums[modeIndex] = std::max(pendingPeakMaximums[modeIndex], modes[modeIndex]);
        }
        if (++pendingPeakCount == peakBlockSize)
        {
            const auto peakCursor = peakWriteCursor.load(std::memory_order_relaxed);
            const auto peakIndex = static_cast<size_t>(peakCursor) & (peakRingCapacity - 1);
            for (size_t modeIndex = 0; modeIndex < modes.size(); ++modeIndex)
            {
                peakMinimumRingBuffers[modeIndex][peakIndex].store(pendingPeakMinimums[modeIndex], std::memory_order_relaxed);
                peakMaximumRingBuffers[modeIndex][peakIndex].store(pendingPeakMaximums[modeIndex], std::memory_order_relaxed);
            }
            // Publish only after all four channels are committed.
            peakWriteCursor.store(peakCursor + 1, std::memory_order_release);
            pendingPeakMinimums.fill(std::numeric_limits<float>::max());
            pendingPeakMaximums.fill(std::numeric_limits<float>::lowest());
            pendingPeakCount = 0;
        }
        for (size_t modeIndex = 0; modeIndex < modes.size(); ++modeIndex)
        {
            widebandMinimumRingBuffers[modeIndex][ringIndex].store(modes[modeIndex], std::memory_order_relaxed);
            widebandMaximumRingBuffers[modeIndex][ringIndex].store(modes[modeIndex], std::memory_order_relaxed);
        }

        for (size_t bandIndex = 0; bandIndex < numBands; ++bandIndex)
        {
            ringBuffers[bandIndex][0][ringIndex].store(
                static_cast<float>(bands[bandIndex].left), std::memory_order_relaxed);
            ringBuffers[bandIndex][1][ringIndex].store(
                static_cast<float>(bands[bandIndex].right), std::memory_order_relaxed);
        }

        ++cursor;
    }

    writeCursor.store(cursor, std::memory_order_release);
}

void MultibandScop::copySince(SampleBatch& destination, uint64_t& readCursor) const
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

    for (size_t modeIndex = 0; modeIndex < scop::analysisChannelCount; ++modeIndex)
    {
        auto& minimums = destination.widebandMinimums[modeIndex];
        auto& maximums = destination.widebandMaximums[modeIndex];
        minimums.resize(sampleCount);
        maximums.resize(sampleCount);

        for (size_t sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex)
        {
            const auto ringIndex = static_cast<size_t>(startCursor + sampleIndex) & (ringCapacity - 1);
            minimums[sampleIndex] = widebandMinimumRingBuffers[modeIndex][ringIndex]
                .load(std::memory_order_relaxed);
            maximums[sampleIndex] = widebandMaximumRingBuffers[modeIndex][ringIndex]
                .load(std::memory_order_relaxed);
        }
    }

    readCursor = endCursor;
}

void MultibandScop::copyWidebandSince(SampleBatch& destination, uint64_t& readCursor) const
{
    const auto endCursor = writeCursor.load(std::memory_order_acquire);
    const auto earliestCursor = endCursor > ringCapacity ? endCursor - ringCapacity : 0;
    const auto startCursor = std::clamp(readCursor, earliestCursor, endCursor);
    const auto sampleCount = static_cast<size_t>(endCursor - startCursor);

    for (size_t modeIndex = 0; modeIndex < scop::analysisChannelCount; ++modeIndex)
    {
        auto& minimums = destination.widebandMinimums[modeIndex];
        auto& maximums = destination.widebandMaximums[modeIndex];
        minimums.resize(sampleCount);
        maximums.resize(sampleCount);
        for (size_t sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex)
        {
            const auto ringIndex = static_cast<size_t>(startCursor + sampleIndex) & (ringCapacity - 1);
            minimums[sampleIndex] = widebandMinimumRingBuffers[modeIndex][ringIndex]
                .load(std::memory_order_relaxed);
            maximums[sampleIndex] = widebandMaximumRingBuffers[modeIndex][ringIndex]
                .load(std::memory_order_relaxed);
        }
    }
    readCursor = endCursor;
}

void MultibandScop::copyWidebandPeaksSince(SampleBatch& destination, uint64_t& readPeakCursor) const
{
    const auto endCursor = peakWriteCursor.load(std::memory_order_acquire);
    const auto earliestCursor = endCursor > peakRingCapacity ? endCursor - peakRingCapacity : 0;
    const auto startCursor = std::clamp(readPeakCursor, earliestCursor, endCursor);
    const auto groupCount = static_cast<size_t>(endCursor - startCursor);

    for (size_t modeIndex = 0; modeIndex < scop::analysisChannelCount; ++modeIndex)
    {
        auto& minimums = destination.widebandMinimums[modeIndex];
        auto& maximums = destination.widebandMaximums[modeIndex];
        minimums.resize(groupCount);
        maximums.resize(groupCount);
        for (size_t groupIndex = 0; groupIndex < groupCount; ++groupIndex)
        {
            const auto ringIndex = static_cast<size_t>(startCursor + groupIndex) & (peakRingCapacity - 1);
            minimums[groupIndex] = peakMinimumRingBuffers[modeIndex][ringIndex]
                .load(std::memory_order_relaxed);
            maximums[groupIndex] = peakMaximumRingBuffers[modeIndex][ringIndex]
                .load(std::memory_order_relaxed);
        }
    }
    readPeakCursor = endCursor;
}

uint64_t MultibandScop::getPeakWriteCursor() const noexcept
{
    return peakWriteCursor.load(std::memory_order_acquire);
}

double MultibandScop::getSampleRate() const noexcept
{
    return sampleRate.load(std::memory_order_acquire);
}

uint64_t MultibandScop::getWriteCursor() const noexcept
{
    return writeCursor.load(std::memory_order_acquire);
}

uint64_t MultibandScop::getResetRevision() const noexcept
{
    return resetRevision.load(std::memory_order_acquire);
}

uint64_t MultibandScop::getSlopeRevision() const noexcept
{
    return slopeCompensator.getAppliedKernelRevision();
}
}
