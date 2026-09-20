#pragma once

namespace ana::lvls
{
inline constexpr float minimumRmsWindowMilliseconds = 10.0f;
inline constexpr float maximumRmsWindowMilliseconds = 3000.0f;
inline constexpr float defaultRmsWindowMilliseconds = 300.0f;
inline constexpr float rmsWindowStepMilliseconds = 10.0f;

inline constexpr float minimumPeakHoldMilliseconds = 0.0f;
inline constexpr float maximumPeakHoldMilliseconds = 10000.0f;
inline constexpr float defaultPeakHoldMilliseconds = 1000.0f;
inline constexpr float peakHoldStepMilliseconds = 10.0f;

inline constexpr int minimumMeterWidth = 106;
inline constexpr int maximumMeterWidth = 180;
inline constexpr int defaultMeterWidth = minimumMeterWidth;

inline constexpr float minimumDisplayDecibels = -99.0f;
inline constexpr float maximumDisplayDecibels = 12.0f;
inline constexpr float displayDecibelStep = 0.1f;
inline constexpr float defaultDisplayLowDecibels = -60.0f;
inline constexpr float defaultDisplayHighDecibels = 0.0f;
}
