#pragma once

#include "DisclosureAnimation.h"

// Hover begins after 50 ms from mouse entry (not from the next paint).
// Once the dwell elapses, the hover is fully visible without an additional fade-in.
class HoverAnimation final : private juce::MouseListener, private juce::Timer
{
public:
    static constexpr int delayMs = 50;

    explicit HoverAnimation(juce::Component& targetComponent,
                            bool wholeComponentIsHoverTarget = true)
        : component(targetComponent),
          wholeComponentHover(wholeComponentIsHoverTarget),
          lastPointerPosition(juce::Desktop::getMousePosition())
    {
        animation.onUpdate = [&targetComponent] { targetComponent.repaint(); };
        targetComponent.addMouseListener(this, true);
    }

    ~HoverAnimation() override { stopTimer(); component.removeMouseListener(this); }

    void inherit(const bool highlighted)
    {
        lastPointerPosition = juce::Desktop::getMousePosition();
        inheritedPosition = lastPointerPosition;
        inheritedHover = highlighted;
        pendingHover = false;
        stopTimer();
        animation.snap(highlighted);
        component.repaint();
    }

    void setHovered(const bool hovered)
    {
        if (! hovered)
        {
            if (pendingHover)
            {
                pendingHover = false;
                stopTimer();
            }
            if (animation.targetExpanded())
            {
                animation.setExpanded(false);
                component.repaint();
            }
        }
        else if (! pendingHover && ! animation.targetExpanded())
        {
            pendingHover = true;
            startTimer(delayMs);
        }
    }

    float amount(const bool active)
    {
        const auto pointerPosition = juce::Desktop::getMousePosition();
        if (inheritedHover)
        {
            if (pointerPosition == inheritedPosition && component.isEnabled()
                && component.getLocalBounds().contains(component.getLocalPoint(nullptr, pointerPosition)))
                return 1.0f;
            inheritedHover = false;
        }
        if (! active && animation.targetExpanded() && component.isEnabled()
            && pointerPosition == lastPointerPosition && coveredByChoicePrompt(pointerPosition))
        {
            animation.snap(true);
            return 1.0f;
        }
        setHovered(active);
        lastPointerPosition = pointerPosition;
        return static_cast<float>(animation.value());
    }

private:
    bool coveredByChoicePrompt(const juce::Point<int> screenPosition) const
    {
        if (! component.getLocalBounds().contains(component.getLocalPoint(nullptr, screenPosition))) return false;
        auto* top = component.getTopLevelComponent();
        auto* target = top->getComponentAt(top->getLocalPoint(nullptr, screenPosition));
        for (; target != nullptr; target = target->getParentComponent())
            if (static_cast<bool>(target->getProperties().getWithDefault("preserveUnderlyingHover", false)))
                return true;
        return false;
    }

    void mouseEnter(const juce::MouseEvent&) override
    {
        if (wholeComponentHover && component.isShowing() && component.isEnabled())
            setHovered(true);
    }

    void mouseMove(const juce::MouseEvent& event) override
    {
        if (animation.targetExpanded())
            lastPointerPosition = event.source.getScreenPosition().roundToInt();
    }
    void mouseExit(const juce::MouseEvent&) override
    {
        if (! component.isMouseOver(true))
        {
            // Let amount() deal with the choice-popup hover preservation
            // before withdrawing a previously visible highlight.
            if (pendingHover)
            {
                pendingHover = false;
                stopTimer();
            }
            component.repaint();
        }
    }
    void timerCallback() override
    {
        stopTimer();
        if (! pendingHover) return;
        pendingHover = false;
        if (component.isShowing() && component.isEnabled() && component.isMouseOver(true))
        {
            animation.snap(true);
            component.repaint();
        }
    }
    void mouseDown(const juce::MouseEvent& event) override
    {
        lastPointerPosition = event.source.getScreenPosition().roundToInt();
    }

    juce::Component& component;
    const bool wholeComponentHover;
    DisclosureAnimation animation { 20.0 };
    juce::Point<int> lastPointerPosition, inheritedPosition;
    bool inheritedHover = false;
    bool pendingHover = false;
};
