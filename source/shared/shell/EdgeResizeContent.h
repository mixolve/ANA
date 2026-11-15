#pragma once

#include "shared/shell/Theme.h"
#include "EdgeResizeHandle.h"

namespace ana::ui
{
class EdgeResizeContent final : public juce::Component
{
public:
    EdgeResizeContent(juce::Component& window, juce::ComponentBoundsConstrainer& constrainer,
                      juce::Component& innerContent, const bool enableRightEdge = true,
                      const bool enableSnapping = true)
        : content(innerContent),
          hasRightEdge(enableRightEdge),
          rightEdge(window, EdgeResizeHandle::Axis::horizontal, constrainer.getMinimumWidth(), constrainer.getMaximumWidth(), false, enableSnapping),
          bottomEdge(window, EdgeResizeHandle::Axis::vertical, constrainer.getMinimumHeight(), constrainer.getMaximumHeight(), false, enableSnapping),
          corner(window, EdgeResizeHandle::Axis::both, std::max(constrainer.getMinimumWidth(), constrainer.getMinimumHeight()), std::min(constrainer.getMaximumWidth(), constrainer.getMaximumHeight()), false, enableSnapping)
    {
        getProperties().set("emptyEditorBackground",true);
        addAndMakeVisible(content);
        rightEdge.setAlwaysOnTop(true);
        bottomEdge.setAlwaysOnTop(true);
        if (hasRightEdge)
            addAndMakeVisible(rightEdge);
        addAndMakeVisible(bottomEdge);
        if (hasRightEdge) addAndMakeVisible(corner);
    }

    void resized() override
    {
        content.setBounds(getLocalBounds());
        if (hasRightEdge)
            rightEdge.setBounds(getWidth() - resizeHandleThickness, 0, resizeHandleThickness, getHeight());
        bottomEdge.setBounds(0, getHeight() - resizeHandleThickness, getWidth(), resizeHandleThickness);
        if (hasRightEdge)
            rightEdge.toFront(false);
        bottomEdge.toFront(false);
        corner.setBounds(getWidth() - resizeHandleThickness, getHeight() - resizeHandleThickness,
                         resizeHandleThickness, resizeHandleThickness);
        corner.toFront(false);
    }

private:
    juce::Component& content;
    bool hasRightEdge;
    EdgeResizeHandle rightEdge;
    EdgeResizeHandle bottomEdge;
    EdgeResizeHandle corner;
};
}
