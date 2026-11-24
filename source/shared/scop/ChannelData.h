#pragma once

#include <array>
#include <cstddef>
#include <vector>

namespace ana::scop
{
inline constexpr size_t analysisChannelCount = 4;
using AnalysisChannelSamples = std::array<float, analysisChannelCount>;

struct Envelope
{
    std::vector<float> minimums;
    std::vector<float> maximums;
};

using AnalysisChannelEnvelopes = std::array<Envelope, analysisChannelCount>;

inline AnalysisChannelSamples makeAnalysisChannelSamples(const float left, const float right) noexcept
{
    return { left, right, 0.5f * (left + right), 0.5f * (left - right) };
}
}
