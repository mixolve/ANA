#pragma once

#include "dsp/Crossover.h"

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

struct ScopeEnvelope
{
    std::vector<float> minimums;
    std::vector<float> maximums;
};

struct OfflineScopeSnapshot
{
    static constexpr size_t numChannelModes = 4;
    using BandEnvelopes = std::array<ScopeEnvelope, numChannelModes>;

    std::array<BandEnvelopes, dsp::Crossover::numRanges> bands;
    size_t activeBandCount = 0;
    double startTimeSeconds = 0.0;
    double durationSeconds = 0.0;
    uint64_t revision = 0;
};

struct OfflineSourceTakeChoice
{
    juce::String sourceId;
    juce::String sourceName;
    juce::String takeId;
    juce::String takeName;
    int takeNumber = 1;
    juce::String audioSourceName;
    juce::String audioSourcePersistentId;
    double playbackStartSeconds = 0.0;
    double playbackDurationSeconds = 0.0;
    double sourceStartSeconds = 0.0;
    double playRate = 1.0;
    bool hostEnumerated = false;
    bool activeTake = false;

    bool operator==(const OfflineSourceTakeChoice& other) const
    {
        return sourceId == other.sourceId
            && sourceName == other.sourceName
            && takeId == other.takeId
            && takeName == other.takeName
            && takeNumber == other.takeNumber
            && audioSourceName == other.audioSourceName
            && audioSourcePersistentId == other.audioSourcePersistentId
            && juce::approximatelyEqual(playbackStartSeconds, other.playbackStartSeconds)
            && juce::approximatelyEqual(playbackDurationSeconds, other.playbackDurationSeconds)
            && juce::approximatelyEqual(sourceStartSeconds, other.sourceStartSeconds)
            && juce::approximatelyEqual(playRate, other.playRate)
            && hostEnumerated == other.hostEnumerated
            && activeTake == other.activeTake;
    }
};

class MultibandScope
{
public:
    static constexpr size_t numBands = dsp::Crossover::numRanges;
    static constexpr size_t numSourceChannels = 2;
    static constexpr size_t ringCapacity = 524288;
    using BandSnapshot = std::array<std::vector<float>, numSourceChannels>;
    using Snapshot = std::array<BandSnapshot, numBands>;

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
    std::atomic<uint64_t> writeCursor { 0 };
    std::atomic<double> sampleRate { 44100.0 };
    size_t displayDecimation = 1;
    size_t displayDecimationCounter = 0;
};
} // namespace ana
