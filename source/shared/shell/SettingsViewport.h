#pragma once

#include "SquareControl.h"

// Accumulate smooth-wheel movement before rounding to viewport pixels.
class SettingsViewport final : public juce::Viewport, private juce::Timer
{
public:
    SettingsViewport() : appearance(*this)
    {
        recreateScrollbars();
        setLookAndFeel(&appearance);
        setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::never);
    }
    ~SettingsViewport() override { stopTimer(); setLookAndFeel(nullptr); }

    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override
    {
        auto* content = getViewedComponent();
        if (content == nullptr) return;
        const auto maximum = juce::jmax(0, content->getHeight() - getHeight());
        const auto delta = std::abs(wheel.deltaY) >= std::abs(wheel.deltaX) ? wheel.deltaY : -wheel.deltaX;
        if (maximum == 0 || std::abs(delta) < 1.0e-6f) return;
        const auto current = getViewPosition();
        if (wheelContent != content || applied != current || !wheel.isSmooth)
        {
            wheelContent = content;
            wheelY = current.y;
        }
        const auto sensitivity = (wheel.isSmooth ? 140.0 : 48.0) * (event.mods.isShiftDown() ? 0.1 : 1.0);
        auto movement = -double(delta) * sensitivity;
        if (!wheel.isSmooth && juce::roundToInt(movement) == 0)
            movement = movement < 0.0 ? -48.0 : 48.0;
        wheelY = juce::jlimit(0.0, double(maximum), wheelY + movement);
        setViewPosition(0, juce::roundToInt(wheelY));
        applied = getViewPosition();
    }

    void visibleAreaChanged(const juce::Rectangle<int>& area) override
    {
        if (area.getY() == previousY) return;
        previousY = area.getY();
        auto& bar = getVerticalScrollBar();
        bar.getProperties().set("scrollActiveUntil", juce::Time::getMillisecondCounterHiRes() + 50.0);
        bar.repaint();
        startTimerHz(60);
    }

private:
    class WheelScrollBar final : public juce::ScrollBar
    {
    public:
        WheelScrollBar(SettingsViewport& viewport, bool vertical) : juce::ScrollBar(vertical), owner(viewport) {}
        void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override
        { owner.mouseWheelMove(event, wheel); }
    private:
        SettingsViewport& owner;
    };

    juce::ScrollBar* createScrollBarComponent(bool vertical) override
    { return new WheelScrollBar(*this, vertical); }

    class Appearance final : public juce::LookAndFeel_V4
    {
    public:
        explicit Appearance(SettingsViewport& viewport) : owner(viewport) {}
        void drawScrollbar(juce::Graphics& g, juce::ScrollBar& bar,
            int x, int y, int width, int height, bool vertical,
            int thumbStart, int thumbSize, bool hovered, bool held) override
        {
            g.setColour(ana::ui::square::panelBackground);
            g.fillRect(x, y, width, height);
            auto& state = bar.getProperties();
            const auto now = juce::Time::getMillisecondCounterHiRes();
            const auto until = double(state.getWithDefault("scrollActiveUntil", 0.0));
            const auto desired = held || now < until ? ana::ui::white : hovered ? ana::ui::light : ana::ui::dark;
            constexpr double fadeMs = 50.0;
            const auto decode = [](const juce::var& value) { return juce::Colour(juce::uint32(juce::int64(value))); };
            auto target = decode(state.getWithDefault("scrollFadeTarget", juce::int64(desired.getARGB())));
            auto from = decode(state.getWithDefault("scrollFadeFrom", juce::int64(target.getARGB())));
            auto started = double(state.getWithDefault("scrollFadeStarted", now - fadeMs));
            auto colour = from.interpolatedWith(target, float(juce::jlimit(0.0, 1.0, (now - started) / fadeMs)));
            if (target != desired)
            {
                from = colour; target = desired; started = now;
                state.set("scrollFadeUntil", now + fadeMs);
                owner.startTimerHz(60);
            }
            state.set("scrollFadeFrom", juce::int64(from.getARGB()));
            state.set("scrollFadeTarget", juce::int64(target.getARGB()));
            state.set("scrollFadeStarted", started);
            colour = from.interpolatedWith(target, float(juce::jlimit(0.0, 1.0, (now - started) / fadeMs)));
            g.setColour(colour);
            if (vertical) g.fillRect(x, y + thumbStart, width, thumbSize);
            else g.fillRect(x + thumbStart, y, thumbSize, height);
        }
    private:
        SettingsViewport& owner;
    } appearance;

    void timerCallback() override
    {
        auto& bar = getVerticalScrollBar();
        auto& state = bar.getProperties();
        const auto now = juce::Time::getMillisecondCounterHiRes();
        const auto activeUntil = double(state.getWithDefault("scrollActiveUntil", 0.0));
        const auto fadeUntil = double(state.getWithDefault("scrollFadeUntil", 0.0));
        if (activeUntil > 0.0 && now >= activeUntil) state.remove("scrollActiveUntil");
        if (fadeUntil > 0.0 && now >= fadeUntil) state.remove("scrollFadeUntil");
        bar.repaint();
        if (now >= activeUntil && now >= fadeUntil) stopTimer();
    }

    juce::Component::SafePointer<juce::Component> wheelContent;
    juce::Point<int> applied;
    double wheelY = 0.0;
    int previousY = 0;
};
