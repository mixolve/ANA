#pragma once

#include <cstddef>
#include <cmath>
#include "UppercaseText.h"
#include <JuceHeader.h>
#include "ControlAppearance.h"

namespace ana::ui
{
inline constexpr float baseFontSize = 22.0f;
inline constexpr int controlHeight = 24;
inline constexpr int dragHandleSlotWidth = 30;
inline constexpr int resizeHandleThickness = 4;
inline constexpr int letterSpacing = 0;
inline constexpr int textPadding = 8;
inline constexpr int borderWidth = 1;
inline constexpr int readoutTextVerticalOffset = 0;

inline juce::Font makeFont()
{
#if JUCE_TARGET_HAS_BINARY_DATA
    static const auto typeface = juce::Typeface::createSystemTypefaceFor(
        BinaryData::IosevkaCharonMonoMedium_ttf,
        static_cast<size_t>(BinaryData::IosevkaCharonMonoMedium_ttfSize));
    if (typeface != nullptr)
        return juce::Font(juce::FontOptions(typeface).withHeight(baseFontSize))
            .withExtraKerningFactor(letterSpacing / baseFontSize);
#endif

    return juce::Font(juce::FontOptions("Iosevka Charon Mono", "Medium", baseFontSize))
        .withExtraKerningFactor(letterSpacing / baseFontSize);
}

inline int getTextPixelWidth(const juce::Font& font, const juce::String& text)
{
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText(font, text, 0.0f, 0.0f);
    return static_cast<int>(std::ceil(glyphs.getBoundingBox(0, -1, true).getWidth()));
}

inline bool drawLoopingText(juce::Graphics& graphics,
                     const juce::String& text,
                     const juce::Rectangle<int>& bounds,
                     const juce::Font& font,
                     const juce::Justification justification = juce::Justification::centred,
                     const double elapsedSeconds = -1.0)
{
    if (text.isEmpty() || bounds.isEmpty())
        return false;

    // Every text run goes through Graphics::drawText so readouts share the same
    // glyph rasterisation (and therefore the same weight) as the rest of the UI.
    const auto drawText = [&](const juce::String& value, juce::Rectangle<float> area,
                              juce::Justification alignment)
    {
        graphics.drawText(value, area, alignment, false);
    };
    const auto textWidth = static_cast<float>(getTextPixelWidth(font, text));

    if (textWidth <= static_cast<float>(bounds.getWidth()))
    {
        drawText(text, bounds.toFloat(), justification);
        return false;
    }

    constexpr auto pixelsPerSecond = 32.0f;
    const auto loopText = text + " :: ";
    const auto loopWidth = static_cast<float>(getTextPixelWidth(font, loopText));
    const auto elapsed = static_cast<float>(elapsedSeconds >= 0.0
        ? elapsedSeconds : juce::Time::getMillisecondCounterHiRes() * 0.001);
    const auto offset = std::fmod(elapsed * pixelsPerSecond, loopWidth);
    const auto textY = static_cast<float>(bounds.getY());
    const auto textHeight = static_cast<float>(bounds.getHeight());
    const auto firstX = static_cast<float>(bounds.getX()) - offset;

    graphics.saveState();
    graphics.reduceClipRegion(bounds);
    drawText(loopText, {firstX, textY, loopWidth + 1.0f, textHeight}, juce::Justification::centredLeft);
    drawText(loopText, {firstX + loopWidth, textY, loopWidth + 1.0f, textHeight}, juce::Justification::centredLeft);
    graphics.restoreState();
    return true;
}

inline int readoutVerticalOffset()
{
    // Centre the visible numeric outlines on whole pixels. Filling glyph
    // outlines at a fractional offset spreads the antialiasing and makes the
    // readouts look lighter than the same font elsewhere, so the correction is
    // applied as an integer translation of the text bounds instead. Use one
    // numeric reference for every value, so changing digits or opening the
    // controller cannot move the baseline.
    static const auto offset = []
    {
        juce::GlyphArrangement reference;
        reference.addLineOfText(makeFont(), "0123456789", 0.0f, 0.0f);
        juce::Path outlines;
        reference.createPath(outlines);
        if (outlines.isEmpty()) return 0;
        return juce::roundToInt(reference.getBoundingBox(0, reference.getNumGlyphs(), false).getCentreY()
            - outlines.getBounds().getCentreY());
    }();
    return offset;
}

// A painted field places its text itself, while JUCE's inline text editor has
// its own (surprising) vertical placement rules. Lay the editor out, measure
// where it really draws the text line, then translate it until that line's
// centre lands where the painted text was drawn, so the text does not move when
// editing starts.
inline void alignInlineEditorText(juce::TextEditor& editor,
                                  const juce::Rectangle<int>& bounds,
                                  const int paintedTextCentreY)
{
    editor.setBounds(bounds);

    const juce::Range<int> range { 0, editor.getTotalNumChars() };

    if (range.isEmpty())
        return;

    const auto textBounds = editor.getTextBounds(range).getBounds();

    if (textBounds.isEmpty())
        return;

    editor.setBounds(bounds.translated(0, paintedTextCentreY - textBounds.getCentreY()));
}

inline void scheduleMarqueeRepaint(juce::Component& component, bool& pending)
{
    if (pending || ! component.isShowing())
        return;

    pending = true;
    juce::Timer::callAfterDelay(16, [safeComponent = juce::Component::SafePointer<juce::Component>(&component), &pending]
    {
        if (safeComponent == nullptr)
            return;

        pending = false;
        safeComponent->repaint();
    });
}


inline int textControlWidth(const juce::String& text) noexcept
{
    return borderWidth * 2 + textPadding * 2
        + getTextPixelWidth(makeFont(), text);
}

inline int textControlWidth(const int characterCount) noexcept
{
    return textControlWidth(juce::String::repeatedString(
        "0", std::max(0, characterCount)));
}

inline juce::Rectangle<int> readoutTextBounds(const juce::Rectangle<int> bounds) noexcept
{
    return bounds.translated(0, readoutTextVerticalOffset);
}

static_assert(dragHandleSlotWidth == 30);

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
inline const auto background = juce::Colour(0xff222222);
inline const auto windowBackground = juce::Colour(0xff666666);

inline juce::Colour opacityShade(const float opacity) noexcept
{
    if (opacity <= 0.0f)
        return background;
    if (opacity < 0.5f)
        return dark;
    if (opacity < 0.8f)
        return light;
    return white;
}

}
