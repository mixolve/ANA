#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace ana::audio_fingerprint
{
inline constexpr int samplesPerSegment = 32;
inline constexpr std::array<double, 4> positions { 0.067, 0.283, 0.571, 0.853 };
inline constexpr uint64_t initialHash = 1469598103934665603ull;
inline constexpr uint64_t prime = 1099511628211ull;

inline uint64_t addChannelCount(uint64_t hash, const int channelCount) noexcept
{
    hash ^= static_cast<uint64_t>(channelCount);
    return hash * prime;
}

template <typename Sample>
uint64_t addSample(uint64_t hash, const Sample sample) noexcept
{
    const auto quantized = static_cast<uint32_t>(std::llround(
        std::clamp(static_cast<double>(sample), -1.0, 1.0) * 8388607.0));
    hash ^= quantized;
    return hash * prime;
}

template <typename SampleProvider>
uint64_t addSegment(uint64_t hash, const int channelCount, SampleProvider&& sampleAt) noexcept
{
    for (int sample = 0; sample < samplesPerSegment; ++sample)
        for (int channel = 0; channel < channelCount; ++channel)
            hash = addSample(hash, sampleAt(sample, channel));

    return hash;
}
}
