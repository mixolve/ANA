#pragma once

#include <JuceHeader.h>

#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace ana::lvls
{
class MeterProcessor
{
public:
    struct ProcessingOptions
    {
        constexpr ProcessingOptions(const bool shouldProcessPeakRms = true,
                                    const bool shouldProcessLoudness = true,
                                    const bool shouldProcessHistory = true) noexcept
            : peakRms(shouldProcessPeakRms),
              loudness(shouldProcessLoudness),
              history(shouldProcessHistory)
        {
        }

        bool peakRms;
        bool loudness;
        bool history;
    };

    struct Values
    {
        std::array<float, 4> peakDecibels { -120.0f, -120.0f, -120.0f, -120.0f };
        std::array<float, 4> rmsDecibels { -120.0f, -120.0f, -120.0f, -120.0f };
        std::array<float, 4> peakMaximumDecibels { -120.0f, -120.0f, -120.0f, -120.0f };
        std::array<float, 4> peakHoldDecibels { -120.0f, -120.0f, -120.0f, -120.0f };
        std::array<float, 4> rmsMaximumDecibels { -120.0f, -120.0f, -120.0f, -120.0f };
        float momentaryLufs = -120.0f;
        float shortTermLufs = -120.0f;
        float integratedLufs = -120.0f;
        float momentaryMaximumLufs = -120.0f;
        float shortTermMaximumLufs = -120.0f;
        float integratedMaximumLufs = -120.0f;
        float loudnessRange = 0.0f;
    };

    static constexpr size_t historyCapacity = 3600;
    static constexpr size_t historySeriesCount = 3;
    enum HistorySeries : size_t { momentaryHistory = 0, shortTermHistory, integratedHistory };
    static constexpr size_t integratedLoudnessBinCount = 941;
    static constexpr size_t maximumBlockSize = 32768;

    void prepare(double newSampleRate, int maximumBlockSamples = static_cast<int>(maximumBlockSize)) noexcept;
    void reset() noexcept;
    void requestClear() noexcept;
    void setFrozen(bool shouldFreeze) noexcept;
    bool isFrozen() const noexcept;
    void processBlock(const juce::AudioBuffer<float>& buffer,
                      float rmsWindowMilliseconds = 300.0f,
                      float peakHoldMilliseconds = 1000.0f,
                      bool holdOnTransportStop = false,
                      ProcessingOptions options = {}) noexcept;
    Values getValues() const noexcept;
    void copyHistory(size_t series, std::vector<float>& destination) const;
    uint64_t getRevision() const noexcept;

    // Filter coefficients are configured outside the audio loop.
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
    void publishHistoryValues(const std::array<float, historySeriesCount>& values) noexcept;
    void updateRollingLoudnessPowers() noexcept;
    void publishLoudnessFrame(bool includeLoudness, bool includeHistory) noexcept;
    void addLoudnessBlock(float power,
                          std::array<double, integratedLoudnessBinCount>& energies,
                          std::array<uint64_t, integratedLoudnessBinCount>& counts) noexcept;
    float getIntegratedLufs() const noexcept;
    float getLoudnessRange() const noexcept;

    std::array<Biquad, 2> highShelves;
    std::array<Biquad, 2> highPasses;
    std::unique_ptr<juce::dsp::Oversampling<float>> truePeakOversampler;
    std::array<std::array<std::atomic<float>, historyCapacity>, historySeriesCount> publishedHistory;
    std::array<std::atomic<float>, 4> publishedPeakDecibels {
        std::atomic<float> { -120.0f }, std::atomic<float> { -120.0f },
        std::atomic<float> { -120.0f }, std::atomic<float> { -120.0f }
    };
    std::array<std::atomic<float>, 4> publishedRmsDecibels {
        std::atomic<float> { -120.0f }, std::atomic<float> { -120.0f },
        std::atomic<float> { -120.0f }, std::atomic<float> { -120.0f }
    };
    std::array<std::atomic<float>, 4> publishedPeakMaximumDecibels {
        std::atomic<float> { -120.0f }, std::atomic<float> { -120.0f },
        std::atomic<float> { -120.0f }, std::atomic<float> { -120.0f }
    };
    std::array<std::atomic<float>, 4> publishedPeakHoldDecibels {
        std::atomic<float> { -120.0f }, std::atomic<float> { -120.0f },
        std::atomic<float> { -120.0f }, std::atomic<float> { -120.0f }
    };
    std::array<std::atomic<float>, 4> publishedRmsMaximumDecibels {
        std::atomic<float> { -120.0f }, std::atomic<float> { -120.0f },
        std::atomic<float> { -120.0f }, std::atomic<float> { -120.0f }
    };
    std::atomic<float> publishedMomentaryLufs { -120.0f };
    std::atomic<float> publishedShortTermLufs { -120.0f };
    std::atomic<float> publishedIntegratedLufs { -120.0f };
    std::atomic<float> publishedMomentaryMaximumLufs { -120.0f };
    std::atomic<float> publishedShortTermMaximumLufs { -120.0f };
    std::atomic<float> publishedIntegratedMaximumLufs { -120.0f };
    std::atomic<float> publishedLoudnessRange { 0.0f };
    std::atomic<size_t> historyWriteCount { 0 };
    std::atomic<uint64_t> revision { 0 };
    std::atomic<bool> clearRequested { false };
    std::atomic<bool> frozen { false };
    double sampleRate = 0.0;
    std::array<std::vector<float>, 4> rmsPowerHistory;
    std::vector<float> loudnessPowerHistory;
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
    std::array<double, integratedLoudnessBinCount> integratedLoudnessEnergies {};
    std::array<uint64_t, integratedLoudnessBinCount> integratedLoudnessCounts {};
    std::array<double, integratedLoudnessBinCount> loudnessRangeEnergies {};
    std::array<uint64_t, integratedLoudnessBinCount> loudnessRangeCounts {};
    size_t integratedFrameWriteCount = 0;
    double pendingLoudnessFrameEnergy = 0.0;
    uint32_t pendingLoudnessFrameSamples = 0;
    uint32_t loudnessFrameSampleCount = 1;
};
} // namespace ana::lvls
