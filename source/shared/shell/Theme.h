#pragma once

#include <cstddef>
#include <JuceHeader.h>

namespace ana::ui
{
inline constexpr float baseFontSize = 22.0f;
inline constexpr float iconFontSize = 18.0f;
inline constexpr int controlHeight = 30;
inline constexpr int iconControlSize = 30;
inline constexpr int activeBorderWidth = 2;
inline constexpr int glyphWidth = 12;
inline constexpr int letterSpacing = 1;
inline constexpr int textPadding = 8;
inline constexpr int borderWidth = 1;
inline constexpr int readoutTextVerticalOffset = -1;

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

inline juce::Rectangle<int> readoutTextBounds(const juce::Rectangle<int> bounds) noexcept
{
    return bounds.translated(0, readoutTextVerticalOffset);
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
        if (width <= 0 || remainingBounds.getWidth() < width)
            return {};
        const auto bounds = remainingBounds.removeFromLeft(width);
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

inline juce::Font makeFont()
{
#if JUCE_TARGET_HAS_BINARY_DATA
    if (auto typeface = juce::Typeface::createSystemTypefaceFor(
            BinaryData::IosevkaCharonMonoMedium_ttf,
            static_cast<size_t>(BinaryData::IosevkaCharonMonoMedium_ttfSize)))
        return juce::Font(juce::FontOptions(typeface).withHeight(baseFontSize))
            .withExtraKerningFactor(letterSpacing / baseFontSize);
#endif

    return juce::Font(juce::FontOptions("Iosevka Charon Mono", "Medium", baseFontSize))
        .withExtraKerningFactor(letterSpacing / baseFontSize);
}
}
