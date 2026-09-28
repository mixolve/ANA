#pragma once

#include "shared/shell/Theme.h"

namespace ana::ui
{
class EdgeResizeContent final : public juce::Component
{
public:
    EdgeResizeContent(juce::Component& window, juce::ComponentBoundsConstrainer& constrainer,
                      juce::Component& innerContent)
        : content(innerContent),
          rightEdge(&window, &constrainer, juce::ResizableEdgeComponent::rightEdge),
          bottomEdge(&window, &constrainer, juce::ResizableEdgeComponent::bottomEdge)
    {
        addAndMakeVisible(content);
        rightEdge.setAlwaysOnTop(true);
        bottomEdge.setAlwaysOnTop(true);
        addAndMakeVisible(rightEdge);
        addAndMakeVisible(bottomEdge);
    }

    void resized() override
    {
        content.setBounds(getLocalBounds());
        rightEdge.setBounds(getWidth() - gap.pixels(), 0, gap.pixels(), getHeight());
        bottomEdge.setBounds(0, getHeight() - gap.pixels(), getWidth(), gap.pixels());
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
    InvisibleEdge rightEdge;
    InvisibleEdge bottomEdge;
};
}
