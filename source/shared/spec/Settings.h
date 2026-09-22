#pragma once

#include <array>

namespace ana::spec
{
enum class ColourMap
{
    multicolor2,
    grayscale
};

inline constexpr std::array<const char*, 2> colourMapLabels {
    "MULTICOLOR 2",
    "GRAYSCALE"
};
inline constexpr int defaultColourMapIndex = 0;

inline ColourMap colourMapFromIndex(const int index) noexcept
{
    return index == static_cast<int>(ColourMap::grayscale)
        ? ColourMap::grayscale : ColourMap::multicolor2;
}

inline constexpr float minimumMapTimeMilliseconds = 10.0f;
inline constexpr float maximumMapTimeMilliseconds = 30000.0f;
inline constexpr float defaultMapTimeMilliseconds = 10000.0f;
inline constexpr float mapTimeStepMilliseconds = 10.0f;
inline constexpr float mapTimeRangeSkewCentreMilliseconds = 5000.0f;

inline constexpr float minimumDisplayDecibels = -200.0f;
inline constexpr float maximumDisplayDecibels = 10.0f;
inline constexpr float defaultDisplayLowDecibels = -96.0f;
inline constexpr float defaultDisplayHighDecibels = 0.0f;
inline constexpr float displayRangeStepDecibels = 0.01f;
inline constexpr float minimumDisplaySpanDecibels = 1.0f;

inline constexpr float minimumSlopeDecibelsPerOctave = -12.0f;
inline constexpr float maximumSlopeDecibelsPerOctave = 12.0f;
inline constexpr float defaultSlopeDecibelsPerOctave = 4.5f;
inline constexpr float slopeStepDecibelsPerOctave = 0.1f;
}
