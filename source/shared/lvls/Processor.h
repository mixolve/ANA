#pragma once

#include "shared/lvls/Settings.h"

#include <cstddef>
#include <cstdint>
#include <JuceHeader.h>

#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace ana::lvls
{
class LvlsProcessor
{
public:
    static constexpr float minimumDecibels = -120.0f;

    struct ProcessingOptions
    {
        constexpr ProcessingOptions(bool peakRmsIn = true,
                                    bool loudnessIn = true,
                                    bool metricsIn = true,
                                    bool wholeDurationIn = false) noexcept
            : peakRms(peakRmsIn), loudness(loudnessIn), metrics(metricsIn),
              wholeDuration(wholeDurationIn) {}

        bool peakRms;
        bool loudness;
        bool metrics;
        bool wholeDuration;
    };

    struct Values
    {
        std::array<float, 4> peakDecibels { minimumDecibels, minimumDecibels, minimumDecibels, minimumDecibels };
        std::array<float, 4> rmsDecibels { minimumDecibels, minimumDecibels, minimumDecibels, minimumDecibels };
        std::array<float, 4> peakMaximumDecibels { minimumDecibels, minimumDecibels, minimumDecibels, minimumDecibels };
        std::array<float, 4> peakHoldDecibels { minimumDecibels, minimumDecibels, minimumDecibels, minimumDecibels };
        std::array<float, 4> rmsMaximumDecibels { minimumDecibels, minimumDecibels, minimumDecibels, minimumDecibels };
        std::array<float, 4> wholeRmsDecibels { minimumDecibels, minimumDecibels, minimumDecibels, minimumDecibels };
        // Negative means no measured energy; the negative half is 100 minus this value.
        float positiveHalfWavePercent = -1.0f;
        float halfWaveRmsDifference = 0.0f;
        float momentaryLufs = minimumDecibels;
        float shortTermLufs = minimumDecibels;
        float integratedLufs = minimumDecibels;
        float momentaryMaximumLufs = minimumDecibels;
        float shortTermMaximumLufs = minimumDecibels;
        float integratedMaximumLufs = minimumDecibels;
        float loudnessRange = 0.0f;
    };

    static constexpr size_t loudnessHistogramBinCount = 941;
    static constexpr size_t maximumBlockSize = 32768;

    void prepare(double newSampleRate, int maximumBlockSamples = static_cast<int>(maximumBlockSize)) noexcept;
    void reset() noexcept;
    void requestClear() noexcept;
    void setFrozen(bool shouldFreeze) noexcept;
    bool isFrozen() const noexcept;
    void processBlock(const juce::AudioBuffer<float>& buffer,
                      float rmsWindowMilliseconds = defaultRmsWindowMilliseconds,
                      float peakHoldMilliseconds = defaultPeakHoldMilliseconds,
                      bool holdOnTransportStop = false,
                      ProcessingOptions options = {}) noexcept;
    Values getValues() const noexcept;
    // Atomically consume the highest true peaks received since the previous
    // readout poll. Meter ballistics are deliberately not involved.
    std::array<float, 4> consumeReadoutPeakGains() noexcept;
    uint64_t getResetEpoch() const noexcept;
    uint64_t getRevision() const noexcept;

    struct Biquad
    {
        void reset() noexcept { z1 = z2 = 0.0f; }
        float process(float input) noexcept;

        float b0 = 1.0f;
        float b1 = 0.0f;
        float b2 = 0.0f;
        float a1 = 0.0f;
        float a2 = 0.0f;
        float z1 = 0.0f;
        float z2 = 0.0f;
    };

private:
    void configureKWeighting() noexcept;
    void updateRollingLoudnessPowers() noexcept;
    void publishLoudnessFrame(bool includeLoudness, bool includeMetrics) noexcept;
    void addLoudnessBlock(float power,
                          std::array<double, loudnessHistogramBinCount>& energies,
                          std::array<uint64_t, loudnessHistogramBinCount>& counts) noexcept;
    float getIntegratedLufs() const noexcept;
    float getLoudnessRange() const noexcept;

    std::array<Biquad, 2> highShelves;
    std::array<Biquad, 2> highPasses;
    std::unique_ptr<juce::dsp::Oversampling<float>> truePeakOversampler;
    size_t truePeakMaximumInputBlockSamples = 1;
    std::array<std::atomic<float>, 4> publishedPeakDecibels {
        std::atomic<float> { minimumDecibels }, std::atomic<float> { minimumDecibels },
        std::atomic<float> { minimumDecibels }, std::atomic<float> { minimumDecibels }
    };
    std::array<std::atomic<float>, 4> publishedRmsDecibels {
        std::atomic<float> { minimumDecibels }, std::atomic<float> { minimumDecibels },
        std::atomic<float> { minimumDecibels }, std::atomic<float> { minimumDecibels }
    };
    std::array<std::atomic<float>, 4> publishedPeakMaximumDecibels {
        std::atomic<float> { minimumDecibels }, std::atomic<float> { minimumDecibels },
        std::atomic<float> { minimumDecibels }, std::atomic<float> { minimumDecibels }
    };
    std::array<std::atomic<float>, 4> publishedPeakHoldDecibels {
        std::atomic<float> { minimumDecibels }, std::atomic<float> { minimumDecibels },
        std::atomic<float> { minimumDecibels }, std::atomic<float> { minimumDecibels }
    };
    std::array<std::atomic<float>, 4> publishedRmsMaximumDecibels {
        std::atomic<float> { minimumDecibels }, std::atomic<float> { minimumDecibels },
        std::atomic<float> { minimumDecibels }, std::atomic<float> { minimumDecibels }
    };
    std::array<std::atomic<float>, 4> publishedWholeRmsDecibels {
        std::atomic<float> { minimumDecibels }, std::atomic<float> { minimumDecibels },
        std::atomic<float> { minimumDecibels }, std::atomic<float> { minimumDecibels }
    };
    std::atomic<float> publishedPositiveHalfWavePercent { -1.0f };
    std::atomic<float> publishedHalfWaveRmsDifference { 0.0f };
    std::atomic<float> publishedMomentaryLufs { minimumDecibels };
    std::atomic<float> publishedShortTermLufs { minimumDecibels };
    std::atomic<float> publishedIntegratedLufs { minimumDecibels };
    std::atomic<float> publishedMomentaryMaximumLufs { minimumDecibels };
    std::atomic<float> publishedShortTermMaximumLufs { minimumDecibels };
    std::atomic<float> publishedIntegratedMaximumLufs { minimumDecibels };
    std::atomic<float> publishedLoudnessRange { 0.0f };
    std::array<std::atomic<float>, 4> readoutPeakGains {
        std::atomic<float> { 0.0f }, std::atomic<float> { 0.0f },
        std::atomic<float> { 0.0f }, std::atomic<float> { 0.0f }
    };
    std::atomic<uint64_t> resetEpoch { 0 };
    std::atomic<uint64_t> revision { 0 };
    std::atomic<bool> clearRequested { false };
    std::atomic<bool> frozen { false };
    double sampleRate = 0.0;
    std::array<std::vector<float>, 4> rmsPowerHistory;
    std::vector<float> loudnessPowerHistory;
    std::vector<double> positiveHalfWavePowerHistory;
    std::vector<double> negativeHalfWavePowerHistory;
    double positiveHalfWavePowerSum = 0.0;
    double negativeHalfWavePowerSum = 0.0;
    double wholePositiveHalfWavePowerSum = 0.0;
    double wholeNegativeHalfWavePowerSum = 0.0;
    std::array<double, 4> wholeRmsPowerSums {};
    uint64_t wholeRmsSampleCount = 0;
    std::array<double, 4> rmsPowerSums {};
    std::array<float, 4> peakGains {};
    std::array<float, 4> peakHoldGains {};
    std::array<uint64_t, 4> peakHoldSamplesRemaining {};
    size_t rmsPowerWriteCount = 0;
    size_t rmsWindowSamples = 1;
    size_t loudnessPowerWriteCount = 0;
    size_t momentaryWindowSamples = 1;
    size_t shortTermWindowSamples = 1;
    double momentaryPowerSum = 0.0;
    double shortTermPowerSum = 0.0;
    float momentaryLufsPower = 0.0f;
    float shortTermLufsPower = 0.0f;
    std::array<double, loudnessHistogramBinCount> integratedLoudnessEnergies {};
    std::array<uint64_t, loudnessHistogramBinCount> integratedLoudnessCounts {};
    std::array<double, loudnessHistogramBinCount> loudnessRangeEnergies {};
    std::array<uint64_t, loudnessHistogramBinCount> loudnessRangeCounts {};
    size_t integratedFrameWriteCount = 0;
    uint32_t pendingLoudnessFrameSamples = 0;
    uint32_t loudnessFrameSampleCount = 1;
};
}
