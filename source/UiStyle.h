#pragma once

#include <JuceHeader.h>

namespace ana::ui
{
inline constexpr float baseFontSize = 22.0f;
inline constexpr int controlHeight = 30;
inline constexpr int gap = 8;
inline const auto white = juce::Colour(0xffffffff);
inline const auto accent = juce::Colour(0xff9999ff);
inline const auto peach = juce::Colour(0xffffcc99);
inline const auto green = juce::Colour(0xff99cc99);
inline const auto red = juce::Colour(0xffff9999);
inline const auto grey800 = juce::Colour(0xff242424);
inline const auto grey700 = juce::Colour(0xff363636);
inline const auto grey500 = juce::Colour(0xff707070);
inline const auto field = juce::Colour(0xff303030);

juce::Font makeFont();
} // namespace ana::ui
