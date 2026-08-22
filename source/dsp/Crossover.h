#pragma once

#include <array>
#include <cstddef>

namespace ana::dsp
{
class Crossover
{
public:
    static constexpr size_t numRanges = 6;
    static constexpr size_t numSplits = 5;

    struct StereoSample
    {
        double left = 0.0;
        double right = 0.0;
    };

    using RangeArray = std::array<StereoSample, numRanges>;
    using SplitFrequencies = std::array<double, numSplits>;

    void prepare(double sampleRate);
    void reset();
    void setActiveSplitCount(size_t newActiveSplitCount);
    void setSplitFrequencies(const SplitFrequencies& newFrequencies);
    RangeArray processSample(double leftInput, double rightInput);

private:
    struct BiquadCoefficients
    {
        double b0 = 0.0;
        double b1 = 0.0;
        double b2 = 0.0;
        double a1 = 0.0;
        double a2 = 0.0;
    };

    struct BiquadState
    {
        double s1 = 0.0;
        double s2 = 0.0;
    };

    using Cascade = std::array<BiquadCoefficients, 4>;
    using CascadeState = std::array<BiquadState, 4>;

    static constexpr size_t numCompensators = 10;

    static double clampFrequency(double frequency, double sampleRate);
    static BiquadCoefficients makeLowpass(double sampleRate, double frequency, double q);
    static BiquadCoefficients makeHighpass(double sampleRate, double frequency, double q);
    static size_t compIndex(size_t splitIndex, size_t rangeIndex);
    static double processCascade(double input, const Cascade& coefficients, CascadeState& state);
    static SplitFrequencies constrainSplitFrequencies(const SplitFrequencies& sourceFrequencies, double sampleRate);

    void updateSplit(size_t splitIndex, double frequency);

    double currentSampleRate = 44100.0;
    SplitFrequencies frequencies { 134.0, 523.0, 2093.0, 5000.0, 10000.0 };
    size_t activeSplitCount = numSplits;
    std::array<Cascade, numSplits> lowpassCoefficients {};
    std::array<Cascade, numSplits> highpassCoefficients {};

    std::array<CascadeState, numSplits> mainLpLeft {};
    std::array<CascadeState, numSplits> mainLpRight {};
    std::array<CascadeState, numSplits> mainHpLeft {};
    std::array<CascadeState, numSplits> mainHpRight {};

    std::array<CascadeState, numCompensators> compLpLeft {};
    std::array<CascadeState, numCompensators> compLpRight {};
    std::array<CascadeState, numCompensators> compHpLeft {};
    std::array<CascadeState, numCompensators> compHpRight {};
};
} // namespace ana::dsp
