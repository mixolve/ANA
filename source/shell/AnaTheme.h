#pragma once

#include <JuceHeader.h>

namespace ana::ui
{
inline constexpr float baseFontSize = 22.0f;
inline constexpr int controlHeight = 30;

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
