#pragma once

#include "shared/shell/Theme.h"

namespace ana::ui
{
class EdgeResizeContent final : public juce::Component
{
public:
    EdgeResizeContent(juce::Component& window, juce::ComponentBoundsConstrainer& constrainer,
                      juce::Component& innerContent, const bool enableRightEdge = true)
        : content(innerContent),
          hasRightEdge(enableRightEdge),
          rightEdge(&window, &constrainer, juce::ResizableEdgeComponent::rightEdge),
          bottomEdge(&window, &constrainer, juce::ResizableEdgeComponent::bottomEdge)
    {
        addAndMakeVisible(content);
        rightEdge.setAlwaysOnTop(true);
        bottomEdge.setAlwaysOnTop(true);
        if (hasRightEdge)
            addAndMakeVisible(rightEdge);
        addAndMakeVisible(bottomEdge);
    }

    void resized() override
    {
        content.setBounds(getLocalBounds());
        if (hasRightEdge)
            rightEdge.setBounds(getWidth() - gap.pixels(), 0, gap.pixels(), getHeight());
        bottomEdge.setBounds(0, getHeight() - gap.pixels(), getWidth(), gap.pixels());
        if (hasRightEdge)
            rightEdge.toFront(false);
        bottomEdge.toFront(false);
    }

private:
    class InvisibleEdge final : public juce::ResizableEdgeComponent
    {
    public:
        using juce::ResizableEdgeComponent::ResizableEdgeComponent;
        void paint(juce::Graphics&) override {}
    };

    juce::Component& content;
    bool hasRightEdge;
    InvisibleEdge rightEdge;
    InvisibleEdge bottomEdge;
};
}
