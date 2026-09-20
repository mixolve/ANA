#pragma once

#include <cstddef>
#include <JuceHeader.h>

#include <array>
#include <cmath>

namespace ana::ara
{
class BandlimitedResampler final
{
public:
    static constexpr int kernelRadius = 64;

    static void prepare() noexcept
    {
        juce::ignoreUnused(getKernelTable());
    }

    static float interpolate(const float* source,
                             const int sourceSampleCount,
                             const double sourcePosition,
                             const double sourceIncrement) noexcept
    {
        if (source == nullptr || sourceSampleCount <= 0)
            return 0.0f;

        const auto centre = static_cast<int>(std::floor(sourcePosition));
        const auto fraction = sourcePosition - static_cast<double>(centre);
        const auto phase = juce::jlimit(0, phaseCount,
            juce::roundToInt(fraction * static_cast<double>(phaseCount)));
        const auto cutoffLevel = sourceIncrement <= 1.0
            ? 0
            : juce::jlimit(0, cutoffLevelCount - 1,
                static_cast<int>(std::ceil(cutoffLevelsPerOctave * std::log2(sourceIncrement))));
        const auto* weights = getKernelTable().get(cutoffLevel, phase);
        auto weightedSample = 0.0f;

        for (auto tap = 0; tap < kernelSize; ++tap)
        {
            const auto sampleIndex = centre - kernelRadius + 1 + tap;
            if (sampleIndex >= 0 && sampleIndex < sourceSampleCount)
                weightedSample += source[sampleIndex] * weights[tap];
        }

        return weightedSample;
    }


private:
    static constexpr int kernelSize = kernelRadius * 2;
    static constexpr int phaseCount = 512;
    static constexpr int cutoffLevelCount = 16;
    static constexpr double cutoffLevelsPerOctave = 4.0;

    struct KernelTable final
    {
        KernelTable() noexcept
        {
            for (auto cutoffLevel = 0; cutoffLevel < cutoffLevelCount; ++cutoffLevel)
            {
                const auto cutoff = std::pow(2.0,
                    -static_cast<double>(cutoffLevel) / cutoffLevelsPerOctave);
                for (auto phase = 0; phase <= phaseCount; ++phase)
                {
                    const auto fraction = static_cast<double>(phase)
                        / static_cast<double>(phaseCount);
                    auto sum = 0.0;
                    auto* coefficients = get(cutoffLevel, phase);
                    for (auto tap = 0; tap < kernelSize; ++tap)
                    {
                        const auto distance = static_cast<double>(tap - kernelRadius + 1)
                            - fraction;
                        const auto normalisedDistance = distance
                            / static_cast<double>(kernelRadius);
                        const auto sincPosition = cutoff * distance;
                        const auto sinc = std::abs(sincPosition) < 1.0e-12
                            ? 1.0
                            : std::sin(juce::MathConstants<double>::pi * sincPosition)
                                / (juce::MathConstants<double>::pi * sincPosition);
                        const auto window = std::abs(normalisedDistance) < 1.0
                            ? 0.42
                                + 0.5 * std::cos(juce::MathConstants<double>::pi
                                                 * normalisedDistance)
                                + 0.08 * std::cos(juce::MathConstants<double>::twoPi
                                                  * normalisedDistance)
                            : 0.0;
                        coefficients[tap] = static_cast<float>(cutoff * sinc * window);
                        sum += coefficients[tap];
                    }

                    if (std::abs(sum) > 1.0e-12)
                        for (auto tap = 0; tap < kernelSize; ++tap)
                            coefficients[tap] = static_cast<float>(coefficients[tap] / sum);
                }
            }
        }

        float* get(const int cutoffLevel, const int phase) noexcept
        {
            return values.data() + (static_cast<size_t>(cutoffLevel) * (phaseCount + 1)
                + static_cast<size_t>(phase)) * kernelSize;
        }

        const float* get(const int cutoffLevel, const int phase) const noexcept
        {
            return values.data() + (static_cast<size_t>(cutoffLevel) * (phaseCount + 1)
                + static_cast<size_t>(phase)) * kernelSize;
        }

        std::array<float, cutoffLevelCount * (phaseCount + 1) * kernelSize> values {};
    };

    static const KernelTable& getKernelTable() noexcept
    {
        static const KernelTable table;
        return table;
    }
};
}
