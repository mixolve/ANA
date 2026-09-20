#pragma once

#include <algorithm>

namespace ana::corr
{
inline constexpr float minimumCoefficient = -1.0f;
inline constexpr float maximumCoefficient = 1.0f;
inline constexpr float defaultLowCoefficient = minimumCoefficient;
inline constexpr float defaultHighCoefficient = maximumCoefficient;
inline constexpr float frequencyModeMinimumCoefficient = 0.0f;
inline constexpr float minimumCoefficientSpan = 0.01f;
inline constexpr float coefficientStep = 0.01f;

constexpr float rangeMinimum(const bool frequencyMode) noexcept
{
    return frequencyMode ? frequencyModeMinimumCoefficient : minimumCoefficient;
}

inline float toInvertedNormalised(const float value, const bool frequencyMode) noexcept
{
    const auto minimum = rangeMinimum(frequencyMode);
    const auto clamped = std::clamp(value, minimum, maximumCoefficient);
    return (maximumCoefficient - clamped) / (maximumCoefficient - minimum);
}

inline float fromInvertedNormalised(const float normalised, const bool frequencyMode) noexcept
{
    const auto minimum = rangeMinimum(frequencyMode);
    return maximumCoefficient
        - std::clamp(normalised, 0.0f, 1.0f) * (maximumCoefficient - minimum);
}
}
