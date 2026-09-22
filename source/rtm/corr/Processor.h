#pragma once

#include "../shell/StereoFftStream.h"

#include <cstdint>
#include <JuceHeader.h>

#include <array>
#include <atomic>

namespace ana::corr
{
class CorrProcessor
{
public:
    enum class Mode
    {
        phase = 0,
        frequency,
        signedCorrelation
    };
    enum class DisplayType { average, minimum };

    static constexpr int modeCount = static_cast<int>(Mode::signedCorrelation) + 1;
    static constexpr int modeIndex(const Mode mode) noexcept { return static_cast<int>(mode); }
    static Mode modeFromIndex(int index) noexcept;

    static constexpr int maximumFftSize = fft::StereoFftStream::maximumFftSize;
    static constexpr int maximumBinCount = maximumFftSize / 2 + 1;

    CorrProcessor();

    void prepare(double newSampleRate) noexcept;
    void reset() noexcept;
    void requestClear() noexcept;
    void setFrozen(bool shouldFreeze) noexcept;
    bool isFrozen() const noexcept;
    void processBlock(const juce::AudioBuffer<float>& buffer, int fftSize,
                      float fftOverlap, float averagingTimeMilliseconds,
                      Mode mode) noexcept;
    void copyCorr(Mode mode, DisplayType type,
                         std::vector<float>& destination, int& fftSize) const;
    double getSampleRate() const noexcept;
    uint64_t getRevision() const noexcept;

private:
    void publish(const fft::StereoFftFrame& frame, float averagingTimeMilliseconds,
                 Mode mode) noexcept;

    using BinValues = std::array<float, maximumBinCount>;
    using AtomicBinValues = std::array<std::atomic<float>, maximumBinCount>;
    using ModeBinValues = std::array<BinValues, modeCount>;
    using AtomicModeBinValues = std::array<AtomicBinValues, modeCount>;

    fft::StereoFftStream fftStream;
    ModeBinValues averages {};
    ModeBinValues minimums {};
    AtomicModeBinValues publishedAverages {};
    AtomicModeBinValues publishedMinimums {};
    std::atomic<int> publishedFftSize { 0 };
    std::atomic<uint64_t> revision { 0 };
    std::atomic<bool> clearRequested { false };
    std::atomic<bool> frozen { false };
    int lastFrameSize = 0;
};
}
