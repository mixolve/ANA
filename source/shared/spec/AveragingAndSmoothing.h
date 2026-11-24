#pragma once

#include <algorithm>
#include <cmath>

namespace ana::spectrum_processing
{
inline constexpr float minimumAveragingTimeMilliseconds = 20.0f;
inline constexpr float maximumAveragingTimeMilliseconds = 10000.0f;
inline constexpr float defaultAveragingTimeMilliseconds = 500.0f;
inline constexpr float averagingTimeStepMilliseconds = 1.0f;

inline constexpr float minimumSmoothingPercent = 0.0f;
inline constexpr float maximumSmoothingPercent = 100.0f;
inline constexpr float defaultSmoothingPercent = 30.0f;
inline constexpr float smoothingStepPercent = 1.0f;
inline constexpr double maximumSmoothingBandwidthOctaves = 1.0 / 3.0;

inline float averagingAlpha(const int hopSize,
                            const double sampleRate,
                            const float averagingTimeMilliseconds) noexcept
{
    if (sampleRate <= 0.0)
        return 0.0f;

    const auto averagingMilliseconds = std::clamp(
        averagingTimeMilliseconds,
        minimumAveragingTimeMilliseconds,
        maximumAveragingTimeMilliseconds);
    const auto frameSeconds = static_cast<float>(std::max(1, hopSize))
        / static_cast<float>(sampleRate);
    const auto averagingSeconds = averagingMilliseconds * 0.001f;

    // At AVG-TIME milliseconds, 99% of a step has been applied.
    constexpr float residualAtSettlingTime = 0.01f;
    return std::exp(std::log(residualAtSettlingTime) * frameSeconds / averagingSeconds);
}

inline double smoothingBandwidthOctaves(const float smoothingPercent) noexcept
{
    if (! std::isfinite(smoothingPercent))
        return 0.0;
    const auto normalised = std::clamp(
        smoothingPercent, minimumSmoothingPercent, maximumSmoothingPercent);
    return static_cast<double>(normalised) / maximumSmoothingPercent
        * maximumSmoothingBandwidthOctaves;
}
}
