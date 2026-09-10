#pragma once

#include "LinkwitzRileyCrossover.h"

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace ana
{
enum class ScopeChannelMode
{
    left,
    right,
    mid,
    side,
    lr,
    ms
};

class MultibandScope
{
public:
    static constexpr size_t numBands = dsp::Crossover::numRanges;
    static constexpr size_t numSourceChannels = 2;
    static constexpr size_t ringCapacity = 524288;
    using BandSnapshot = std::array<std::vector<float>, numSourceChannels>;
    struct Snapshot
    {
        std::array<BandSnapshot, numBands> bands;
        BandSnapshot wideband;

        BandSnapshot& operator[](const size_t index) noexcept { return bands[index]; }
        const BandSnapshot& operator[](const size_t index) const noexcept { return bands[index]; }
        BandSnapshot& front() noexcept { return bands.front(); }
        const BandSnapshot& front() const noexcept { return bands.front(); }
    };

    MultibandScope();

    void prepare(double newSampleRate);
    void reset();
    void setCrossoverSettings(size_t activeSplitCount,
                              const dsp::Crossover::SplitFrequencies& frequencies);
    void processBlock(const juce::AudioBuffer<float>& buffer) noexcept;
    void copySince(Snapshot& destination, uint64_t& readCursor) const;

    double getSampleRate() const noexcept;
    uint64_t getWriteCursor() const noexcept;

private:
    static_assert((ringCapacity & (ringCapacity - 1)) == 0, "Ring capacity must be a power of two");

    dsp::Crossover crossover;
    dsp::Crossover::SplitFrequencies currentFrequencies { 134.0, 523.0, 2093.0, 5000.0, 10000.0 };
    size_t currentActiveSplitCount = dsp::Crossover::numSplits;
    std::array<std::array<std::array<std::atomic<float>, ringCapacity>, numSourceChannels>, numBands> ringBuffers;
    std::array<std::array<std::atomic<float>, ringCapacity>, numSourceChannels> widebandRingBuffers;
    std::atomic<uint64_t> writeCursor { 0 };
    std::atomic<double> sampleRate { 44100.0 };
    size_t displayDecimation = 1;
    size_t displayDecimationCounter = 0;
};
} // namespace ana
