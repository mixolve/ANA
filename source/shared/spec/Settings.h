#pragma once

namespace ana::spec
{
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
