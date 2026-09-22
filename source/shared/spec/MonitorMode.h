#pragma once

#include <array>
#include <cstddef>

namespace ana::spec
{
enum class MonitorMode : int
{
    stereo,
    leftRight,
    left,
    right,
    midSide,
    mid,
    side
};

inline constexpr size_t monitorModeCount = static_cast<size_t>(MonitorMode::side) + 1;
inline constexpr std::array<const char*, monitorModeCount> monitorModeLabels {
    "ST", "LR", "L", "R", "MS", "M", "S"
};

constexpr int monitorModeIndex(const MonitorMode mode) noexcept
{
    return static_cast<int>(mode);
}

constexpr bool supportsSplitView(const MonitorMode mode) noexcept
{
    return mode == MonitorMode::leftRight || mode == MonitorMode::midSide;
}
}
