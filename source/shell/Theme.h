#pragma once

#include <JuceHeader.h>

namespace ana::ui
{
inline constexpr float baseFontSize = 20.0f;
inline constexpr float iconFontSize = 18.0f;
inline constexpr int controlHeight = 30;
inline constexpr int iconControlSize = 30;
inline constexpr int activeBorderWidth = 2;
inline constexpr int glyphWidth = 12;
inline constexpr int letterSpacing = 1;
inline constexpr int textPadding = 8;
inline constexpr int borderWidth = 1;

constexpr int textControlWidth(const int characterCount) noexcept
{
    const auto count = characterCount > 0 ? characterCount : 0;
    return borderWidth * 2 + textPadding * 2 + glyphWidth * count
        + letterSpacing * std::max(0, count - 1);
}

inline int textControlWidth(const juce::String& text) noexcept
{
    return textControlWidth(text.length());
}

static_assert(textControlWidth(1) == 30);
static_assert(textControlWidth(2) == 43);
static_assert(iconControlSize == 30);

class FixedGap final
{
public:
    static constexpr int pixels() noexcept { return 8; }

    void removeFromLeft(juce::Rectangle<int>& bounds) const noexcept
    {
        bounds.removeFromLeft(std::min(pixels(), bounds.getWidth()));
    }

    void removeFromRight(juce::Rectangle<int>& bounds) const noexcept
    {
        bounds.removeFromRight(std::min(pixels(), bounds.getWidth()));
    }

    void removeFromTop(juce::Rectangle<int>& bounds) const noexcept
    {
        bounds.removeFromTop(std::min(pixels(), bounds.getHeight()));
    }

    void removeFromBottom(juce::Rectangle<int>& bounds) const noexcept
    {
        bounds.removeFromBottom(std::min(pixels(), bounds.getHeight()));
    }
};

inline constexpr FixedGap gap {};
static_assert(FixedGap::pixels() == 8);

class FixedGapRow final
{
public:
    explicit FixedGapRow(juce::Rectangle<int> bounds) noexcept : remainingBounds(bounds) {}

    juce::Rectangle<int> takeLeft(const int width) noexcept
    {
        const auto bounds = remainingBounds.removeFromLeft(
            std::min(std::max(0, width), remainingBounds.getWidth()));
        gap.removeFromLeft(remainingBounds);
        return bounds;
    }

    juce::Rectangle<int> remaining() const noexcept { return remainingBounds; }

private:
    juce::Rectangle<int> remainingBounds;
};

inline const auto white = juce::Colour(0xffffffff);
inline const auto light = juce::Colour(0xffbbbbbb);
inline const auto dark = juce::Colour(0xff444444);
inline const auto black = juce::Colour(0xff000000);

inline const auto accent = white;
inline const auto peach = light;
inline const auto green = dark;
inline const auto red = white;
inline const auto grey800 = black;
inline const auto grey700 = dark;
inline const auto grey500 = light;
inline const auto field = dark;
inline const auto hover = light;
inline const auto secondaryGraph = dark;

inline juce::Colour opacityShade(const float opacity) noexcept
{
    if (opacity <= 0.0f)
        return black;
    if (opacity < 0.5f)
        return dark;
    if (opacity < 0.8f)
        return light;
    return white;
}

juce::Font makeFont();
} // namespace ana::ui
