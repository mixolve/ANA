#pragma once
#include <JuceHeader.h>
namespace gui_export_layout
{
inline int viewportOverflow(const juce::Component& component)
{
    int extra = 0;
    if (const auto* viewport = dynamic_cast<const juce::Viewport*>(&component))
        if (const auto* content = viewport->getViewedComponent())
            extra = juce::jmax(0, content->getHeight() - viewport->getHeight());
    for (auto* child : component.getChildren())
        if (child->isVisible()) extra = juce::jmax(extra, viewportOverflow(*child));
    return extra;
}
}
