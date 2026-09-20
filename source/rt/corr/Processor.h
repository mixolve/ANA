#pragma once

#include "../shell/StereoFftStream.h"

#include <cstdint>
#include <JuceHeader.h>

#include <array>
#include <atomic>
#include <vector>

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
    struct MinimumWindowState
    {
        std::array<double, maximumBinCount> numeratorWindowSum {};
        std::array<double, maximumBinCount> denominatorWindowSum {};
        std::vector<double> numeratorRing;
        std::vector<double> denominatorRing;
        int windowFrames = 0;
        int ringPosition = 0;
        int frameCount = 0;
        int binCount = 0;
        int hopSize = 0;
    };

    void publish(const fft::StereoFftFrame& frame, float averagingTimeMilliseconds,
                 Mode mode) noexcept;
    void updateMinimumWindow(const fft::StereoFftFrame& frame, Mode mode,
                             MinimumWindowState& state,
                             std::array<float, maximumBinCount>& minimum,
                             std::array<std::atomic<float>, maximumBinCount>& published) noexcept;
    static void resetMinimumWindowState(MinimumWindowState& state) noexcept;

    using BinValues = std::array<float, maximumBinCount>;
    using AtomicBinValues = std::array<std::atomic<float>, maximumBinCount>;
    using ModeBinValues = std::array<BinValues, modeCount>;
    using AtomicModeBinValues = std::array<AtomicBinValues, modeCount>;

    fft::StereoFftStream fftStream;
    ModeBinValues averages {};
    ModeBinValues minimums {};
    std::array<MinimumWindowState, modeCount> rtMinimumStates;
    AtomicModeBinValues publishedAverages {};
    AtomicModeBinValues publishedMinimums {};
    std::atomic<int> publishedFftSize { 0 };
    std::atomic<uint64_t> revision { 0 };
    std::atomic<bool> clearRequested { false };
    std::atomic<bool> frozen { false };
    int lastFrameSize = 0;
};
}
