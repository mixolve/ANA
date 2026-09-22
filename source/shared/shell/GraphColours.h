#pragma once

#include <JuceHeader.h>

#include <array>

namespace ana::ui
{
struct GraphColourOption
{
    const char* name;
    juce::uint32 argb;
};

inline constexpr std::array<GraphColourOption, 11> graphColourOptions {{
    { "FF9999", 0xffff9999 },
    { "FFCC99", 0xffffcc99 },
    { "FFFF99", 0xffffff99 },
    { "99FF99", 0xff99ff99 },
    { "99FFFF", 0xff99ffff },
    { "9999FF", 0xff9999ff },
    { "CC99FF", 0xffcc99ff },
    { "FF99CC", 0xffff99cc },
    { "FFFFFF", 0xffffffff },
    { "BBBBBB", 0xffbbbbbb },
    { "444444", 0xff444444 }
}};

inline constexpr int defaultGraphColourIndex = 8;
inline constexpr int defaultFirstGraphColourIndex = defaultGraphColourIndex;
inline constexpr int defaultSecondGraphColourIndex = defaultGraphColourIndex;

inline int clampGraphColourIndex(const int index) noexcept
{
    return juce::jlimit(0, static_cast<int>(graphColourOptions.size()) - 1, index);
}

inline juce::Colour graphColour(const int index) noexcept
{
    return juce::Colour(graphColourOptions[static_cast<size_t>(
        clampGraphColourIndex(index))].argb);
}

inline juce::StringArray graphColourNames()
{
    juce::StringArray names;
    for (const auto& option : graphColourOptions)
        names.add(option.name);
    return names;
}
}
