#pragma once

#include "shared/scop/Crossover.h"
#include "shared/scop/Settings.h"

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
    using BandSamples = std::array<std::vector<float>, numSourceChannels>;
    struct SampleBatch
    {
        std::array<BandSamples, numBands> bands;
        BandSamples wideband;

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
    void processBlock(const juce::AudioBuffer<float>& buffer) noexcept;
    void copySince(SampleBatch& destination, uint64_t& readCursor) const;

    double getSampleRate() const noexcept;
    uint64_t getWriteCursor() const noexcept;

private:
    static_assert((ringCapacity & (ringCapacity - 1)) == 0, "Ring capacity must be a power of two");

    dsp::LinkwitzRileyCrossover crossover;
    dsp::LinkwitzRileyCrossover::CrossoverFrequencies currentFrequencies =
        dsp::LinkwitzRileyCrossover::defaultFrequencies;
    size_t currentCrossoverCount = dsp::LinkwitzRileyCrossover::numCrossovers;
    std::array<std::array<std::array<std::atomic<float>, ringCapacity>, numSourceChannels>, numBands> ringBuffers;
    std::array<std::array<std::atomic<float>, ringCapacity>, numSourceChannels> widebandRingBuffers;
    std::atomic<uint64_t> writeCursor { 0 };
    std::atomic<double> sampleRate { 44100.0 };
    size_t displayDecimation = 1;
    size_t displayDecimationCounter = 0;
};
}
