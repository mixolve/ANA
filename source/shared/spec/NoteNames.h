#pragma once

#include <JuceHeader.h>
#include <array>
#include <cmath>

namespace ana::ui
{
inline juce::String cursorNoteName(const float frequency)
{
    if (! std::isfinite(frequency) || frequency <= 0.0f)
        return {};
    static constexpr std::array<const char*, 12> names {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };
    const auto midi = juce::roundToInt(69.0 + 12.0 * std::log2(frequency / 440.0f));
    return juce::String(names[static_cast<size_t>((midi % 12 + 12) % 12)])
        + juce::String(static_cast<int>(std::floor(static_cast<double>(midi) / 12.0)) - 1);
}
}
