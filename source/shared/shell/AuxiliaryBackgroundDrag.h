#pragma once

#include "WindowDragHandle.h"

// Observe all descendants without stealing button/slider/viewport events.
// Only surfaces explicitly marked as empty background start a window drag.
class AuxiliaryBackgroundDrag final : private juce::MouseListener
{
public:
    explicit AuxiliaryBackgroundDrag(juce::Component& window) : moveHandle(window, false) {}
    ~AuxiliaryBackgroundDrag() override { detach(); }

    void setSnappingEnabled(bool enabled) { moveHandle.setSnappingEnabled(enabled); }

    void attach(juce::Component& root)
    {
        detach();
        observedRoot = &root;
        root.addMouseListener(this, true);
    }

    void detach()
    {
        if (observedRoot != nullptr)
            observedRoot->removeMouseListener(this);
        observedRoot = nullptr;
    }

private:
    void mouseDown(const juce::MouseEvent& event) override
    {
        if (! event.mods.isLeftButtonDown() || event.originalComponent == nullptr)
            return;
        if (! static_cast<bool>(event.originalComponent->getProperties()["emptyEditorBackground"]))
            return;
        if (observedRoot != nullptr && bool(observedRoot->getProperties()["topGapDragOnly"]))
        {
            const auto point=event.getEventRelativeTo(observedRoot).getPosition();
            if(point.y < 2 || point.y >= 16) return;
        }
        moveHandle.beginDragAt(event.source.getScreenPosition().roundToInt());
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (! moveHandle.isDragging())
            return;
        if (event.mods.isLeftButtonDown())
            moveHandle.dragTo(event.source.getScreenPosition().roundToInt());
        else
            moveHandle.endDragAt(event.source.getScreenPosition().roundToInt());
    }

    void mouseUp(const juce::MouseEvent& event) override
    {
        if (moveHandle.isDragging())
            moveHandle.endDragAt(event.source.getScreenPosition().roundToInt());
    }

    juce::Component::SafePointer<juce::Component> observedRoot;
    WindowDragHandle moveHandle;
};
