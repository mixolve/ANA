#pragma once

#include <algorithm>
#include <array>
#include <cstddef>

namespace ana::scop
{
inline constexpr std::array<const char*, 9> noteLengthLabels {
    "1/16", "1/8", "1/4", "1/2", "1/1", "2/1", "4/1", "8/1", "16/1"
};
inline constexpr std::array<double, noteLengthLabels.size()> wholeNoteDivisors {
    16.0, 8.0, 4.0, 2.0, 1.0, 0.5, 0.25, 0.125, 0.0625
};
inline constexpr int defaultNoteLengthIndex = 4;

constexpr int clampNoteLengthIndex(const int index) noexcept
{
    return std::clamp(index, 0, static_cast<int>(noteLengthLabels.size()) - 1);
}

inline double noteLengthMilliseconds(const int index, const double bpm) noexcept
{
    const auto safeBpm = std::max(1.0, bpm);
    return 240000.0 / (safeBpm * wholeNoteDivisors[static_cast<size_t>(clampNoteLengthIndex(index))]);
}
}
