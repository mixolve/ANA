#pragma once

#include <JuceHeader.h>
#include <algorithm>
#include <cmath>
#include <functional>

class DisclosureAnimation final : private juce::Timer
{
public:
    static constexpr double durationMs = 80.0;
    explicit DisclosureAnimation(const double duration = durationMs) : transitionDurationMs(duration) {}
    std::function<void()> onUpdate;

    double value() const noexcept { return current; }
    bool isRunning() const noexcept { return running; }
    bool targetExpanded() const noexcept { return target > 0.0; }

    void snap(bool expanded) noexcept
    {
        stopTimer();
        current = start = target = expanded ? 1.0 : 0.0;
        running = false;
    }

    void setExpanded(bool expanded, double now = juce::Time::getMillisecondCounterHiRes())
    {
        const auto destination = expanded ? 1.0 : 0.0;
        if (expanded == targetExpanded()) return;
        if (running) advance(now, false);
        start = current;
        target = destination;
        startedAt = now;
        // Reversing a partly finished transition must not take a full duration.
        travelMs = transitionDurationMs * std::abs(target - start);
        running = travelMs > 0.0;
        if (running) startTimerHz(120);
        else stopTimer();
    }

    void advance(double now, bool notify = true)
    {
        if (! running) return;
        const auto progress = std::clamp((now - startedAt) / travelMs, 0.0, 1.0);
        const auto eased = progress * progress * (3.0 - 2.0 * progress);
        current = start + (target - start) * eased;
        if (progress >= 1.0)
        {
            current = target;
            running = false;
            stopTimer();
        }
        if (notify && onUpdate) onUpdate();
    }

private:
    void timerCallback() override { advance(juce::Time::getMillisecondCounterHiRes()); }
    const double transitionDurationMs;
    double current = 0.0, start = 0.0, target = 0.0;
    double startedAt = 0.0, travelMs = durationMs;
    bool running = false;
};
