#pragma once

#include "shared/shell/Theme.h"

#include <JuceHeader.h>
#include <algorithm>
#include <cmath>

// A framed, transparent track-name viewport with scrolling for long names.
class TrackLocationLabel final : public juce::Component,
                                 public juce::SettableTooltipClient,
                                 private juce::Timer
{
public:
    TrackLocationLabel() { setInterceptsMouseClicks(false, false); }

    const juce::String& getText() const noexcept { return text; }

    void setText(const juce::String& newText)
    {
        if (text == newText)
            return;
        text = newText;
        textWidth = juce::GlyphArrangement::getStringWidthInt(ana::ui::makeFont(), text);
        resetScroll();
        updateTimer();
        repaint();
    }

    void paint(juce::Graphics& graphics) override
    {
        graphics.setColour(ana::ui::light);
        graphics.drawRect(getLocalBounds(), ana::ui::borderWidth);
        const juce::Graphics::ScopedSaveState savedState(graphics);
        const auto content = getTextBounds();
        graphics.reduceClipRegion(content);
        graphics.setColour(ana::ui::white);
        graphics.setFont(ana::ui::makeFont());
        if (textWidth <= content.getWidth())
        {
            graphics.drawText(text, content, juce::Justification::centred, false);
            return;
        }
        const auto left = content.getX() - juce::roundToInt(scrollOffset);
        graphics.drawText(text, left, content.getY(), std::max(content.getWidth(), textWidth), content.getHeight(),
                          juce::Justification::centredLeft, false);
        if (textWidth > content.getWidth())
            graphics.drawText(text, left + textWidth + 2 * ana::ui::gap.pixels(), content.getY(),
                              textWidth, content.getHeight(), juce::Justification::centredLeft, false);
    }

    void resized() override { resetScroll(); updateTimer(); }
    void visibilityChanged() override { resetScroll(); updateTimer(); }

private:
    juce::Rectangle<int> getTextBounds() const noexcept
    {
        return getLocalBounds().reduced(ana::ui::borderWidth + ana::ui::textPadding,
                                        ana::ui::borderWidth);
    }

    void resetScroll()
    {
        scrollOffset = 0.0;
        lastTickMilliseconds = juce::Time::getMillisecondCounterHiRes();
        pauseUntilMilliseconds = lastTickMilliseconds + 1000.0;
    }

    void updateTimer()
    {
        if (isVisible() && getTextBounds().getWidth() > 0 && textWidth > getTextBounds().getWidth())
            startTimerHz(30);
        else
            stopTimer();
    }

    void timerCallback() override
    {
        const auto now = juce::Time::getMillisecondCounterHiRes();
        const auto elapsed = std::clamp(now - lastTickMilliseconds, 0.0, 100.0);
        lastTickMilliseconds = now;
        if (now < pauseUntilMilliseconds)
            return;
        constexpr double pixelsPerSecond = 30.0;
        scrollOffset = std::fmod(scrollOffset + elapsed * pixelsPerSecond / 1000.0,
                               static_cast<double>(textWidth + 2 * ana::ui::gap.pixels()));
        repaint();
    }

    juce::String text;
    int textWidth = 0;
    double scrollOffset = 0.0;
    double lastTickMilliseconds = 0.0;
    double pauseUntilMilliseconds = 0.0;
};
