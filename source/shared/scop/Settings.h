#pragma once

#include <cstddef>

namespace ana
{
enum class ScopChannelMode
{
    left,
    right,
    mid,
    side,
    lr,
    ms,
    st // Append to preserve the stored indices of existing channel modes.
};

inline constexpr size_t scopChannelModeCount = static_cast<size_t>(ScopChannelMode::st) + 1;
}

namespace ana::scop
{
inline constexpr float minimumTimeMilliseconds = 10.0f;
inline constexpr float maximumTimeMilliseconds = 30000.0f;
inline constexpr float defaultTimeMilliseconds = 10000.0f;
inline constexpr float timeStepMilliseconds = 10.0f;
inline constexpr float timeRangeSkewCentreMilliseconds = 5000.0f;

inline constexpr float minimumOpacityPercent = 10.0f;
inline constexpr float maximumOpacityPercent = 100.0f;
inline constexpr float defaultOpacityPercent = 100.0f;
inline constexpr float opacityStepPercent = 1.0f;

// Flat tilt compensation is applied to the SCOP analysis copy before crossover.
inline constexpr float minimumSlopeDecibelsPerOctave = -12.0f;
inline constexpr float maximumSlopeDecibelsPerOctave = 12.0f;
inline constexpr float defaultSlopeDecibelsPerOctave = 0.0f;
inline constexpr float slopeStepDecibelsPerOctave = 0.01f;

inline constexpr float minimumVerticalZoomDecibels = -48.0f;
inline constexpr float maximumVerticalZoomDecibels = 96.0f;
inline constexpr float defaultVerticalZoomDecibels = 0.0f;
inline constexpr float verticalZoomStepDecibels = 0.1f;

inline constexpr auto defaultChannelMode = ScopChannelMode::st;
inline constexpr bool defaultFilledStyle = true;
inline constexpr bool defaultZoomControlsVisible = true;
inline constexpr bool defaultMonitorControlsVisible = true;
inline constexpr bool defaultToolsVisible = true;
inline constexpr bool defaultBandNormalized = false;
}
