#pragma once

#include "../shell/StereoFftStream.h"
#include "shared/spec/Channels.h"

#include <cstdint>
#include <JuceHeader.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <functional>
#include <vector>

namespace ana::spec
{
class SpecProcessor
{
public:
    enum class DisplayType { average, maximum, current };

    static constexpr int maximumFftSize = fft::StereoFftStream::maximumFftSize;
    static constexpr int maximumBinCount = maximumFftSize / 2 + 1;
    static constexpr float minimumDecibels = -300.0f;

    // Time Overlap oversamples STFT frames without changing rt MAP scroll cadence.
    static constexpr int mapBaseRasterOversampling = 8;
    static constexpr int mapTimeOverlapChoiceCount = 5;
    static int mapBaseHopSizeForFftSize(int fftSize) noexcept
    {
        return std::max(1, fftSize / mapBaseRasterOversampling);
    }
    static int mapTimeOversamplingFactor(int choiceIndex) noexcept
    {
        static constexpr std::array<int, mapTimeOverlapChoiceCount> factors { 1, 2, 4, 8, 16 };
        return factors[static_cast<size_t>(juce::jlimit(0, mapTimeOverlapChoiceCount - 1, choiceIndex))];
    }
    static float mapTimeOverlapFraction(int choiceIndex) noexcept
    {
        return 1.0f - 1.0f / static_cast<float>(mapTimeOversamplingFactor(choiceIndex));
    }

    SpecProcessor();

    void prepare(double newSampleRate);
    void reset();
    void requestClear() noexcept;
    void setFrozen(bool shouldFreeze) noexcept;
    bool isFrozen() const noexcept;
    using AraFrameCallback = std::function<void(const fft::StereoFftFrame&)>;
    void processAraBlock(const juce::AudioBuffer<float>& buffer,
                             int fftSize,
                             float fftOverlap) noexcept;
    void processAraMapBlock(const juce::AudioBuffer<float>& buffer,
                                int fftSize,
                                int hopSize,
                                const AraFrameCallback& onFrame) noexcept;
    void resetAraStream() noexcept;
    void copySpectrum(Channel channel, DisplayType type,
                  std::vector<float>& destination, int& fftSize) const;
    double getSampleRate() const noexcept;
    uint64_t getRevision() const noexcept;
    uint64_t getClearRevision() const noexcept;

private:
    void publishAra(const fft::StereoFftFrame& frame) noexcept;
    void resetAraAccumulators() noexcept;
    void resetMaximums() noexcept;

    using BinValues = std::array<float, maximumBinCount>;
    using AtomicBinValues = std::array<std::atomic<float>, maximumBinCount>;
    using ChannelBinValues = std::array<BinValues, channelCount>;
    using AtomicChannelBinValues = std::array<AtomicBinValues, channelCount>;
    using AraSumValues = std::array<double, maximumBinCount>;
    using ChannelAraSumValues = std::array<AraSumValues, channelCount>;
    using AraCountValues = std::array<uint64_t, maximumBinCount>;

    fft::StereoFftStream fftStream;
    fft::StereoFftStream mapFftStream;
    ChannelBinValues maximumLevels {};
    AtomicChannelBinValues publishedAverageLevels {};
    AtomicChannelBinValues publishedMaximumLevels {};
    AtomicChannelBinValues publishedCurrentLevels {};
    ChannelAraSumValues araPowerSums {};
    AraCountValues araMagnitudeCount {};
    std::atomic<int> publishedFftSize { 0 };
    std::atomic<uint64_t> revision { 0 };
    std::atomic<uint64_t> clearRevision { 0 };
    std::atomic<bool> clearRequested { false };
    std::atomic<bool> frozen { false };
    int lastFrameSize = 0;
};
}
