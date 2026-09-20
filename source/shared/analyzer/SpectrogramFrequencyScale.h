#pragma once

#include <algorithm>
#include <cmath>

namespace ana::spectrogram_frequency
{
inline constexpr float kneeHz = 100.0f;

inline float scaleCoordinate(const float frequency) noexcept
{
    const auto clampedFrequency = std::max(0.0f, frequency);
    if (clampedFrequency < kneeHz)
        return clampedFrequency / kneeHz - 1.0f;

    return std::log2(clampedFrequency / kneeHz);
}

inline float frequencyFromCoordinate(const float coordinate) noexcept
{
    if (coordinate < 0.0f)
        return kneeHz * std::max(0.0f, coordinate + 1.0f);

    return kneeHz * std::pow(2.0f, coordinate);
}

inline float frequencyAt(const float lowFrequency,
                         const float highFrequency,
                         const float normalised) noexcept
{
    const auto low = std::max(0.0f, lowFrequency);
    const auto high = std::max(low + 1.0e-6f, highFrequency);
    const auto lowCoordinate = scaleCoordinate(low);
    const auto highCoordinate = scaleCoordinate(high);
    const auto t = std::clamp(normalised, 0.0f, 1.0f);
    return frequencyFromCoordinate(lowCoordinate + t * (highCoordinate - lowCoordinate));
}

inline float normalisedForFrequency(const float lowFrequency,
                                    const float highFrequency,
                                    const float frequency) noexcept
{
    const auto low = std::max(0.0f, lowFrequency);
    const auto high = std::max(low + 1.0e-6f, highFrequency);
    const auto clampedFrequency = std::clamp(frequency, low, high);
    const auto lowCoordinate = scaleCoordinate(low);
    const auto highCoordinate = scaleCoordinate(high);
    const auto span = highCoordinate - lowCoordinate;
    if (std::abs(span) <= 1.0e-12f)
        return 0.0f;

    return std::clamp((scaleCoordinate(clampedFrequency) - lowCoordinate) / span,
                      0.0f, 1.0f);
}
}
