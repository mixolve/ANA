#pragma once

#include <array>
#include <cstddef>

namespace ana::dsp
{
class LinkwitzRileyCrossover
{
public:
    static constexpr size_t numBands = 6;
    static constexpr size_t numCrossovers = 5;

    struct StereoSample
    {
        double left = 0.0;
        double right = 0.0;
    };

    using BandArray = std::array<StereoSample, numBands>;
    using CrossoverFrequencies = std::array<double, numCrossovers>;
    inline static constexpr CrossoverFrequencies defaultFrequencies {
        134.0, 523.0, 2093.0, 5000.0, 10000.0
    };

    void prepare(double sampleRate);
    void reset();
    void setCrossoverCount(size_t newCrossoverCount);
    void setCrossoverFrequencies(const CrossoverFrequencies& newFrequencies);
    BandArray processSample(double leftInput, double rightInput);

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

    static constexpr size_t numCompensators = (numCrossovers * (numCrossovers - 1)) / 2;

    static double clampFrequency(double frequency, double sampleRate);
    static BiquadCoefficients makeLowpass(double sampleRate, double frequency, double q);
    static BiquadCoefficients makeHighpass(double sampleRate, double frequency, double q);
    static size_t compIndex(size_t crossoverIndex, size_t bandIndex);
    static double processCascade(double input, const Cascade& coefficients, CascadeState& state);
    static CrossoverFrequencies constrainCrossoverFrequencies(const CrossoverFrequencies& sourceFrequencies, double sampleRate);

    void updateCrossover(size_t crossoverIndex, double frequency);

    double currentSampleRate = 44100.0;
    CrossoverFrequencies frequencies = defaultFrequencies;
    size_t crossoverCount = numCrossovers;
    std::array<Cascade, numCrossovers> lowpassCoefficients {};
    std::array<Cascade, numCrossovers> highpassCoefficients {};

    std::array<CascadeState, numCrossovers> mainLpLeft {};
    std::array<CascadeState, numCrossovers> mainLpRight {};
    std::array<CascadeState, numCrossovers> mainHpLeft {};
    std::array<CascadeState, numCrossovers> mainHpRight {};

    std::array<CascadeState, numCompensators> compLpLeft {};
    std::array<CascadeState, numCompensators> compLpRight {};
    std::array<CascadeState, numCompensators> compHpLeft {};
    std::array<CascadeState, numCompensators> compHpRight {};
};
}
