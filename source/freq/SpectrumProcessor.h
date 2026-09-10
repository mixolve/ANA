#pragma once

#include "../shared/StereoFftStream.h"

#include <JuceHeader.h>

#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace ana::freq
{
class SpectrumProcessor
{
public:
    enum class Channel { stereo, left, right, mid, side };
    enum class DisplayType { realtimeAverage, maximum };

    static constexpr int maximumFftSize = fft::StereoFftStream::maximumFftSize;
    static constexpr int maximumBinCount = maximumFftSize / 2 + 1;

    SpectrumProcessor();

    void prepare(double newSampleRate);
    void reset();
    void requestClear() noexcept;
    void setFrozen(bool shouldFreeze) noexcept;
    bool isFrozen() const noexcept;
    void processBlock(const juce::AudioBuffer<float>& buffer,
                      int blockSize,
                      float overlap,
                      float averagingTimeMilliseconds) noexcept;
    void copySpectrum(Channel channel, DisplayType type,
                      std::vector<float>& destination, int& fftSize) const;
    double getSampleRate() const noexcept;
    uint64_t getRevision() const noexcept;

private:
    void publish(const fft::StereoFftFrame& frame,
                 float averagingTimeMilliseconds) noexcept;

    fft::StereoFftStream fftStream;
    std::array<float, maximumBinCount> leftAverage {};
    std::array<float, maximumBinCount> rightAverage {};
    std::array<float, maximumBinCount> stereoAverage {};
    std::array<float, maximumBinCount> leftMaximum {};
    std::array<float, maximumBinCount> rightMaximum {};
    std::array<float, maximumBinCount> stereoMaximum {};
    std::array<float, maximumBinCount> midAverage {};
    std::array<float, maximumBinCount> sideAverage {};
    std::array<float, maximumBinCount> midMaximum {};
    std::array<float, maximumBinCount> sideMaximum {};
    std::array<std::atomic<float>, maximumBinCount> publishedLeftAverage;
    std::array<std::atomic<float>, maximumBinCount> publishedRightAverage;
    std::array<std::atomic<float>, maximumBinCount> publishedStereoAverage;
    std::array<std::atomic<float>, maximumBinCount> publishedLeftMaximum;
    std::array<std::atomic<float>, maximumBinCount> publishedRightMaximum;
    std::array<std::atomic<float>, maximumBinCount> publishedStereoMaximum;
    std::array<std::atomic<float>, maximumBinCount> publishedMidAverage;
    std::array<std::atomic<float>, maximumBinCount> publishedSideAverage;
    std::array<std::atomic<float>, maximumBinCount> publishedMidMaximum;
    std::array<std::atomic<float>, maximumBinCount> publishedSideMaximum;
    std::atomic<int> publishedFftSize { 0 };
    std::atomic<uint64_t> revision { 0 };
    std::atomic<bool> clearRequested { false };
    std::atomic<bool> frozen { false };
    int lastFrameSize = 0;
};
} // namespace ana::freq
