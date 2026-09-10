#pragma once

#include "../shared/StereoFftStream.h"

#include <JuceHeader.h>

#include <array>
#include <atomic>
#include <vector>

namespace ana::corr
{
class StereoProcessor
{
public:
    enum class Mode { phase, amplitude };
    enum class DisplayType { realtimeAverage, maximum };

    static constexpr int maximumFftSize = fft::StereoFftStream::maximumFftSize;
    static constexpr int maximumBinCount = maximumFftSize / 2 + 1;

    StereoProcessor();

    void prepare(double newSampleRate) noexcept;
    void reset() noexcept;
    void requestClear() noexcept;
    void setFrozen(bool shouldFreeze) noexcept;
    bool isFrozen() const noexcept;
    void processBlock(const juce::AudioBuffer<float>& buffer, int blockSize,
                      float overlap, float averagingTimeMilliseconds) noexcept;
    void copyCorrelation(Mode mode, DisplayType type,
                         std::vector<float>& destination, int& fftSize) const;
    double getSampleRate() const noexcept;
    uint64_t getRevision() const noexcept;

private:
    void publish(const fft::StereoFftFrame& frame, float averagingTimeMilliseconds) noexcept;

    fft::StereoFftStream fftStream;
    std::array<float, maximumBinCount> phaseAverage {};
    std::array<float, maximumBinCount> amplitudeAverage {};
    std::array<float, maximumBinCount> phaseMinimum {};
    std::array<float, maximumBinCount> amplitudeMinimum {};
    std::array<std::atomic<float>, maximumBinCount> publishedPhase;
    std::array<std::atomic<float>, maximumBinCount> publishedAmplitude;
    std::array<std::atomic<float>, maximumBinCount> publishedPhaseMinimum;
    std::array<std::atomic<float>, maximumBinCount> publishedAmplitudeMinimum;
    std::atomic<int> publishedFftSize { 0 };
    std::atomic<uint64_t> revision { 0 };
    std::atomic<bool> clearRequested { false };
    std::atomic<bool> frozen { false };
    int lastFrameSize = 0;
};
} // namespace ana::corr
