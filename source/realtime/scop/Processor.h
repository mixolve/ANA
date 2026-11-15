#pragma once

#include "shared/scop/Crossover.h"
#include "shared/scop/Settings.h"
#include "shared/scop/AnalysisTypes.h"
#include "shared/scop/SlopeCompensator.h"

#include <cstddef>
#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace ana
{
class MultibandScop
{
public:
    static constexpr size_t numBands = dsp::LinkwitzRileyCrossover::numBands;
    static constexpr size_t numSourceChannels = 2;
    static constexpr size_t ringCapacity = 524288;
    // Audio-thread min/max pyramid's coarse level. One bucket per 64 original
    // samples, so a 30-second overview survives GUI stalls even at 384 kHz.
    static constexpr size_t peakBlockSize = 64;
    static constexpr size_t peakRingCapacity = 262144;
    using BandSamples = std::array<std::vector<float>, numSourceChannels>;
    using ModeSamples = std::array<std::vector<float>, scop::analysisChannelCount>;
    struct SampleBatch
    {
        std::array<BandSamples, numBands> bands;
        // L/R/M/S extrema are kept at the native sample rate.
        // Offline analysis uses the same native-sample extrema.
        ModeSamples widebandMinimums;
        ModeSamples widebandMaximums;

        BandSamples& operator[](const size_t index) noexcept { return bands[index]; }
        const BandSamples& operator[](const size_t index) const noexcept { return bands[index]; }
        BandSamples& front() noexcept { return bands.front(); }
        const BandSamples& front() const noexcept { return bands.front(); }
    };

    MultibandScop();

    void prepare(double newSampleRate);
    void reset();
    void setCrossoverSettings(size_t crossoverCount,
                              const dsp::LinkwitzRileyCrossover::CrossoverFrequencies& frequencies);
    void setSlope(float decibelsPerOctave) noexcept;
    void processBlock(const juce::AudioBuffer<float>& buffer) noexcept;
    void copySince(SampleBatch& destination, uint64_t& readCursor) const;
    void copyWidebandSince(SampleBatch& destination, uint64_t& readCursor) const;
    void copyWidebandPeaksSince(SampleBatch& destination, uint64_t& readPeakCursor) const;
    uint64_t getPeakWriteCursor() const noexcept;

    double getSampleRate() const noexcept;
    uint64_t getWriteCursor() const noexcept;
    uint64_t getResetRevision() const noexcept;
    uint64_t getSlopeRevision() const noexcept;

private:
    static_assert((ringCapacity & (ringCapacity - 1)) == 0, "Ring capacity must be a power of two");
    static_assert((peakRingCapacity & (peakRingCapacity - 1)) == 0, "Peak ring capacity must be a power of two");

    dsp::LinkwitzRileyCrossover crossover;
    scop::SlopeCompensator slopeCompensator;
    dsp::LinkwitzRileyCrossover::CrossoverFrequencies currentFrequencies =
        dsp::LinkwitzRileyCrossover::defaultFrequencies;
    size_t currentCrossoverCount = dsp::LinkwitzRileyCrossover::numCrossovers;
    std::array<std::array<std::array<std::atomic<float>, ringCapacity>, numSourceChannels>, numBands> ringBuffers;
    std::array<std::array<std::atomic<float>, ringCapacity>, scop::analysisChannelCount> widebandMinimumRingBuffers;
    std::array<std::array<std::atomic<float>, ringCapacity>, scop::analysisChannelCount> widebandMaximumRingBuffers;
    // Unlike the native ring, these extrema are accumulated on the audio thread
    // before the display thread ever receives or decimates samples.
    std::array<std::array<std::atomic<float>, peakRingCapacity>, scop::analysisChannelCount> peakMinimumRingBuffers;
    std::array<std::array<std::atomic<float>, peakRingCapacity>, scop::analysisChannelCount> peakMaximumRingBuffers;
    scop::AnalysisChannelSamples pendingPeakMinimums {};
    scop::AnalysisChannelSamples pendingPeakMaximums {};
    size_t pendingPeakCount = 0;
    uint64_t peakAccumulatorSlopeRevision = 0;
    std::atomic<uint64_t> peakWriteCursor { 0 };
    std::atomic<uint64_t> writeCursor { 0 };
    std::atomic<uint64_t> resetRevision { 0 };
    std::atomic<double> sampleRate { 44100.0 };
};
}
