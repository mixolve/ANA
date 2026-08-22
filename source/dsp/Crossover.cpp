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
constexpr double minSplitGapHz = 1.0;
constexpr double nyquistMargin = 0.499;
constexpr double pi = 3.14159265358979323846;
}

double Crossover::clampFrequency(const double frequency, const double sampleRate)
{
    return std::clamp(frequency, minFrequency, nyquistMargin * sampleRate);
}

Crossover::BiquadCoefficients Crossover::makeLowpass(const double sampleRate,
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

Crossover::BiquadCoefficients Crossover::makeHighpass(const double sampleRate,
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

size_t Crossover::compIndex(const size_t splitIndex, const size_t rangeIndex)
{
    return (splitIndex * (splitIndex - 1)) / 2 + rangeIndex;
}

double Crossover::processCascade(double input, const Cascade& coefficients, CascadeState& state)
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

Crossover::SplitFrequencies Crossover::constrainSplitFrequencies(const SplitFrequencies& sourceFrequencies,
                                                                 const double sampleRate)
{
    SplitFrequencies constrained {};
    const auto maxFrequency = nyquistMargin * sampleRate;

    for (size_t splitIndex = 0; splitIndex < constrained.size(); ++splitIndex)
    {
        const auto lowerBound = splitIndex == 0 ? minFrequency
                                                : constrained[splitIndex - 1] + minSplitGapHz;
        const auto remainingSplits = static_cast<double>(constrained.size() - splitIndex - 1);
        const auto upperBound = std::max(lowerBound, maxFrequency - (remainingSplits * minSplitGapHz));
        constrained[splitIndex] = std::clamp(sourceFrequencies[splitIndex], lowerBound, upperBound);
    }

    return constrained;
}

void Crossover::prepare(const double sampleRate)
{
    currentSampleRate = sampleRate;

    for (size_t splitIndex = 0; splitIndex < numSplits; ++splitIndex)
        updateSplit(splitIndex, frequencies[splitIndex]);

    reset();
}

void Crossover::reset()
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

void Crossover::setSplitFrequencies(const SplitFrequencies& newFrequencies)
{
    frequencies = constrainSplitFrequencies(newFrequencies, currentSampleRate);

    for (size_t splitIndex = 0; splitIndex < numSplits; ++splitIndex)
        updateSplit(splitIndex, frequencies[splitIndex]);

    reset();
}

void Crossover::setActiveSplitCount(const size_t newActiveSplitCount)
{
    activeSplitCount = std::min(newActiveSplitCount, numSplits);
    reset();
}

void Crossover::updateSplit(const size_t splitIndex, const double frequency)
{
    lowpassCoefficients[splitIndex] = {
        makeLowpass(currentSampleRate, frequency, qA),
        makeLowpass(currentSampleRate, frequency, qB),
        makeLowpass(currentSampleRate, frequency, qA),
        makeLowpass(currentSampleRate, frequency, qB),
    };

    highpassCoefficients[splitIndex] = {
        makeHighpass(currentSampleRate, frequency, qA),
        makeHighpass(currentSampleRate, frequency, qB),
        makeHighpass(currentSampleRate, frequency, qA),
        makeHighpass(currentSampleRate, frequency, qB),
    };
}

Crossover::RangeArray Crossover::processSample(const double leftInput, const double rightInput)
{
    RangeArray ranges {};
    auto remainderLeft = leftInput;
    auto remainderRight = rightInput;

    for (size_t splitIndex = 0; splitIndex < activeSplitCount; ++splitIndex)
    {
        ranges[splitIndex].left = processCascade(remainderLeft, lowpassCoefficients[splitIndex], mainLpLeft[splitIndex]);
        ranges[splitIndex].right = processCascade(remainderRight, lowpassCoefficients[splitIndex], mainLpRight[splitIndex]);
        remainderLeft = processCascade(remainderLeft, highpassCoefficients[splitIndex], mainHpLeft[splitIndex]);
        remainderRight = processCascade(remainderRight, highpassCoefficients[splitIndex], mainHpRight[splitIndex]);
    }

    ranges[activeSplitCount].left = remainderLeft;
    ranges[activeSplitCount].right = remainderRight;

    for (size_t rangeIndex = 0; rangeIndex < activeSplitCount; ++rangeIndex)
    {
        auto compensatedLeft = ranges[rangeIndex].left;
        auto compensatedRight = ranges[rangeIndex].right;

        for (size_t splitIndex = rangeIndex + 1; splitIndex < activeSplitCount; ++splitIndex)
        {
            const auto compensatorIndex = compIndex(splitIndex, rangeIndex);
            compensatedLeft = processCascade(compensatedLeft, lowpassCoefficients[splitIndex], compLpLeft[compensatorIndex])
                            + processCascade(compensatedLeft, highpassCoefficients[splitIndex], compHpLeft[compensatorIndex]);
            compensatedRight = processCascade(compensatedRight, lowpassCoefficients[splitIndex], compLpRight[compensatorIndex])
                             + processCascade(compensatedRight, highpassCoefficients[splitIndex], compHpRight[compensatorIndex]);
        }

        ranges[rangeIndex].left = compensatedLeft;
        ranges[rangeIndex].right = compensatedRight;
    }

    return ranges;
}
} // namespace ana::dsp
