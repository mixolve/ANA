#include "Crossover.h"

#include <algorithm>
#include <cmath>

namespace ana::dsp
{
namespace
{
constexpr double qA = 0.541196100146197;
constexpr double qB = 1.306562964876377;
constexpr double minFrequency = 10.0;
constexpr double minCrossoverGapHz = 1.0;
constexpr double nyquistMargin = 0.499;
constexpr double minimumSampleRate = minFrequency / nyquistMargin;
constexpr double pi = 3.14159265358979323846;
}

double LinkwitzRileyCrossover::clampFrequency(const double frequency, const double sampleRate)
{
    return std::clamp(frequency, minFrequency, nyquistMargin * sampleRate);
}

LinkwitzRileyCrossover::BiquadCoefficients LinkwitzRileyCrossover::makeLowpass(const double sampleRate,
                                                     const double frequency,
                                                     const double q)
{
    const auto clampedFrequency = clampFrequency(frequency, sampleRate);
    const auto w0 = 2.0 * pi * clampedFrequency / sampleRate;
    const auto cs = std::cos(w0);
    const auto sn = std::sin(w0);
    const auto alpha = sn / (2.0 * q);
    const auto b0 = (1.0 - cs) * 0.5;
    const auto b1 = 1.0 - cs;
    const auto b2 = (1.0 - cs) * 0.5;
    const auto a0 = 1.0 + alpha;
    const auto a1 = -2.0 * cs;
    const auto a2 = 1.0 - alpha;

    return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
}

LinkwitzRileyCrossover::BiquadCoefficients LinkwitzRileyCrossover::makeHighpass(const double sampleRate,
                                                      const double frequency,
                                                      const double q)
{
    const auto clampedFrequency = clampFrequency(frequency, sampleRate);
    const auto w0 = 2.0 * pi * clampedFrequency / sampleRate;
    const auto cs = std::cos(w0);
    const auto sn = std::sin(w0);
    const auto alpha = sn / (2.0 * q);
    const auto b0 = (1.0 + cs) * 0.5;
    const auto b1 = -(1.0 + cs);
    const auto b2 = (1.0 + cs) * 0.5;
    const auto a0 = 1.0 + alpha;
    const auto a1 = -2.0 * cs;
    const auto a2 = 1.0 - alpha;

    return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
}

size_t LinkwitzRileyCrossover::compIndex(const size_t crossoverIndex, const size_t bandIndex)
{
    return (crossoverIndex * (crossoverIndex - 1)) / 2 + bandIndex;
}

double LinkwitzRileyCrossover::processCascade(double input, const Cascade& coefficients, CascadeState& state)
{
    auto output = input;

    for (size_t stageIndex = 0; stageIndex < coefficients.size(); ++stageIndex)
    {
        const auto& stageCoefficients = coefficients[stageIndex];
        auto& stageState = state[stageIndex];
        const auto next = stageCoefficients.b0 * output + stageState.s1;
        stageState.s1 = stageCoefficients.b1 * output - stageCoefficients.a1 * next + stageState.s2;
        stageState.s2 = stageCoefficients.b2 * output - stageCoefficients.a2 * next;
        output = next;
    }

    return output;
}

LinkwitzRileyCrossover::CrossoverFrequencies LinkwitzRileyCrossover::constrainCrossoverFrequencies(const CrossoverFrequencies& sourceFrequencies,
                                                                 const double sampleRate)
{
    CrossoverFrequencies constrained {};
    const auto maxFrequency = nyquistMargin * sampleRate;

    for (size_t crossoverIndex = 0; crossoverIndex < constrained.size(); ++crossoverIndex)
    {
        const auto lowerBound = crossoverIndex == 0 ? minFrequency
                                                : constrained[crossoverIndex - 1] + minCrossoverGapHz;
        const auto remainingCrossovers = static_cast<double>(constrained.size() - crossoverIndex - 1);
        const auto upperBound = std::max(lowerBound, maxFrequency - (remainingCrossovers * minCrossoverGapHz));
        constrained[crossoverIndex] = std::clamp(sourceFrequencies[crossoverIndex], lowerBound, upperBound);
    }

    return constrained;
}

void LinkwitzRileyCrossover::prepare(const double sampleRate)
{
    currentSampleRate = std::max(minimumSampleRate, sampleRate);

    for (size_t crossoverIndex = 0; crossoverIndex < numCrossovers; ++crossoverIndex)
        updateCrossover(crossoverIndex, frequencies[crossoverIndex]);

    reset();
}

void LinkwitzRileyCrossover::reset()
{
    mainLpLeft = {};
    mainLpRight = {};
    mainHpLeft = {};
    mainHpRight = {};
    compLpLeft = {};
    compLpRight = {};
    compHpLeft = {};
    compHpRight = {};
}

void LinkwitzRileyCrossover::setCrossoverFrequencies(const CrossoverFrequencies& newFrequencies)
{
    frequencies = constrainCrossoverFrequencies(newFrequencies, currentSampleRate);

    for (size_t crossoverIndex = 0; crossoverIndex < numCrossovers; ++crossoverIndex)
        updateCrossover(crossoverIndex, frequencies[crossoverIndex]);

    reset();
}

void LinkwitzRileyCrossover::setCrossoverCount(const size_t newCrossoverCount)
{
    crossoverCount = std::min(newCrossoverCount, numCrossovers);
    reset();
}

void LinkwitzRileyCrossover::updateCrossover(const size_t crossoverIndex, const double frequency)
{
    lowpassCoefficients[crossoverIndex] = {
        makeLowpass(currentSampleRate, frequency, qA),
        makeLowpass(currentSampleRate, frequency, qB),
        makeLowpass(currentSampleRate, frequency, qA),
        makeLowpass(currentSampleRate, frequency, qB),
    };

    highpassCoefficients[crossoverIndex] = {
        makeHighpass(currentSampleRate, frequency, qA),
        makeHighpass(currentSampleRate, frequency, qB),
        makeHighpass(currentSampleRate, frequency, qA),
        makeHighpass(currentSampleRate, frequency, qB),
    };
}

LinkwitzRileyCrossover::BandArray LinkwitzRileyCrossover::processSample(const double leftInput, const double rightInput)
{
    BandArray bands {};
    auto remainderLeft = leftInput;
    auto remainderRight = rightInput;

    for (size_t crossoverIndex = 0; crossoverIndex < crossoverCount; ++crossoverIndex)
    {
        bands[crossoverIndex].left = processCascade(remainderLeft, lowpassCoefficients[crossoverIndex], mainLpLeft[crossoverIndex]);
        bands[crossoverIndex].right = processCascade(remainderRight, lowpassCoefficients[crossoverIndex], mainLpRight[crossoverIndex]);
        remainderLeft = processCascade(remainderLeft, highpassCoefficients[crossoverIndex], mainHpLeft[crossoverIndex]);
        remainderRight = processCascade(remainderRight, highpassCoefficients[crossoverIndex], mainHpRight[crossoverIndex]);
    }

    bands[crossoverCount].left = remainderLeft;
    bands[crossoverCount].right = remainderRight;

    for (size_t bandIndex = 0; bandIndex < crossoverCount; ++bandIndex)
    {
        auto compensatedLeft = bands[bandIndex].left;
        auto compensatedRight = bands[bandIndex].right;

        for (size_t crossoverIndex = bandIndex + 1; crossoverIndex < crossoverCount; ++crossoverIndex)
        {
            const auto compensatorIndex = compIndex(crossoverIndex, bandIndex);
            compensatedLeft = processCascade(compensatedLeft, lowpassCoefficients[crossoverIndex], compLpLeft[compensatorIndex])
                            + processCascade(compensatedLeft, highpassCoefficients[crossoverIndex], compHpLeft[compensatorIndex]);
            compensatedRight = processCascade(compensatedRight, lowpassCoefficients[crossoverIndex], compLpRight[compensatorIndex])
                             + processCascade(compensatedRight, highpassCoefficients[crossoverIndex], compHpRight[compensatorIndex]);
        }

        bands[bandIndex].left = compensatedLeft;
        bands[bandIndex].right = compensatedRight;
    }

    return bands;
}
}
