#pragma once

#include <algorithm>
#include <cmath>

namespace ana::analyzer_frequency
{
enum class Scale
{
    linear,
    extendedLogarithmic,
    logarithmic,
    mel
};

inline constexpr float minimumHz = 20.0f;
inline constexpr float maximumHz = 20000.0f;
inline constexpr float minimumSpanHz = 1.0f;
inline constexpr float parameterStepHz = 0.01f;
inline constexpr float logSpanRatio = maximumHz / minimumHz;
inline constexpr float extendedLogKneeHz = 100.0f;
inline constexpr int defaultScaleIndex = static_cast<int>(Scale::logarithmic);

inline Scale scaleFromIndex(const int index) noexcept
{
    return static_cast<Scale>(std::clamp(index, 0, static_cast<int>(Scale::mel)));
}

inline float scaleCoordinate(const Scale scale, const float frequency) noexcept
{
    const auto clampedFrequency = std::max(0.0f, frequency);
    switch (scale)
    {
        case Scale::linear:
            return clampedFrequency;
        case Scale::logarithmic:
            return clampedFrequency < extendedLogKneeHz
                ? clampedFrequency / extendedLogKneeHz - 1.0f
                : std::log2(clampedFrequency / extendedLogKneeHz);
        case Scale::mel:
            return 2595.0f * std::log10(1.0f + clampedFrequency / 700.0f);
        case Scale::extendedLogarithmic:
            return std::log(std::max(1.0e-6f, clampedFrequency));
    }

    return clampedFrequency;
}

inline float frequencyFromCoordinate(const Scale scale, const float coordinate) noexcept
{
    switch (scale)
    {
        case Scale::linear:
            return std::max(0.0f, coordinate);
        case Scale::logarithmic:
            return coordinate < 0.0f
                ? extendedLogKneeHz * std::max(0.0f, coordinate + 1.0f)
                : extendedLogKneeHz * std::pow(2.0f, coordinate);
        case Scale::mel:
            return 700.0f * (std::pow(10.0f, coordinate / 2595.0f) - 1.0f);
        case Scale::extendedLogarithmic:
            return std::exp(coordinate);
    }

    return std::max(0.0f, coordinate);
}

inline float normalisedForFrequency(const Scale scale,
                                    const float lowFrequency,
                                    const float highFrequency,
                                    const float frequency) noexcept
{
    const auto low = std::max(0.0f, lowFrequency);
    const auto high = std::max(low + 1.0e-6f, highFrequency);
    const auto lowCoordinate = scaleCoordinate(scale, low);
    const auto highCoordinate = scaleCoordinate(scale, high);
    const auto span = highCoordinate - lowCoordinate;
    if (std::abs(span) <= 1.0e-12f)
        return 0.0f;

    return std::clamp(
        (scaleCoordinate(scale, std::clamp(frequency, low, high)) - lowCoordinate) / span,
        0.0f, 1.0f);
}

inline float frequencyAt(const Scale scale,
                         const float lowFrequency,
                         const float highFrequency,
                         const float normalised) noexcept
{
    const auto low = std::max(0.0f, lowFrequency);
    const auto high = std::max(low + 1.0e-6f, highFrequency);
    const auto lowCoordinate = scaleCoordinate(scale, low);
    const auto highCoordinate = scaleCoordinate(scale, high);
    return frequencyFromCoordinate(
        scale, lowCoordinate + std::clamp(normalised, 0.0f, 1.0f)
            * (highCoordinate - lowCoordinate));
}

inline float toLogNormalised(const float frequency) noexcept
{
    return normalisedForFrequency(Scale::extendedLogarithmic, minimumHz, maximumHz, frequency);
}

inline float fromLogNormalised(const float normalised) noexcept
{
    return frequencyAt(Scale::extendedLogarithmic, minimumHz, maximumHz, normalised);
}
}
