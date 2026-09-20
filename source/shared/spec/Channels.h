#pragma once

#include <cstddef>

namespace ana::spec
{
enum class Channel : size_t
{
    stereo,
    left,
    right,
    mid,
    side,
    delta
};

inline constexpr size_t channelCount = static_cast<size_t>(Channel::delta) + 1;

constexpr size_t channelIndex(const Channel channel) noexcept
{
    return static_cast<size_t>(channel);
}
}
