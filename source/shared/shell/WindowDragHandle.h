#pragma once

#include "Theme.h"
#include "WindowGeometry.h"

class WindowDragHandle final : public juce::Component
{
public:
    explicit WindowDragHandle(juce::Component& component, const bool snapToEdges = true)
        : owner(&component), snappingEnabled(snapToEdges)
    {
        setName("window-drag-area");
        setMouseCursor(juce::MouseCursor::NormalCursor);
        setWantsKeyboardFocus(false);
        setMouseClickGrabsKeyboardFocus(false);
    }

    ~WindowDragHandle() override { finishDrag(); }

    void paint(juce::Graphics&) override {}

    void beginDragAt(const juce::Point<int> screenPosition)
    {
        finishDrag();
        if (owner == nullptr) return;
        dragging = true;
        movedDuringDrag = false;
        const auto bounds = window_geometry::getBounds(*owner);
        startWindowPosition = bounds.getPosition();
        draggedWindowSize = { bounds.getWidth(), bounds.getHeight() };
        snapTargets = snappingEnabled ? window_geometry::getSnapTargets(*owner)
                                      : std::vector<window_geometry::SnapTarget>{};
        startPointerPosition = screenPosition;
        cursorHidden = true;
        window_geometry::setWindowDragActive(*owner, true);
    }

    void dragTo(const juce::Point<int> screenPosition)
    {
        if (! dragging) return;
        if (owner == nullptr) { finishDrag(); return; }
        movedDuringDrag = movedDuringDrag || screenPosition != startPointerPosition;
        window_geometry::setPosition(*owner, dragPosition(screenPosition));
        if (cursorHidden) window_geometry::hideWindowDragCursor();
    }

    void endDragAt(const juce::Point<int> screenPosition)
    {
        if (dragging && owner != nullptr)
            window_geometry::setPosition(*owner, dragPosition(screenPosition));
        finishDrag();
    }

    bool isDragging() const noexcept { return dragging; }
    void setSnappingEnabled(bool enabled) { snappingEnabled=enabled; }

    void mouseDown(const juce::MouseEvent& event) override
    {
        if (event.mods.isLeftButtonDown())
            beginDragAt(event.source.getScreenPosition().roundToInt());
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (! event.mods.isLeftButtonDown()) { finishDrag(); return; }
        dragTo(event.source.getScreenPosition().roundToInt());
    }

    void mouseUp(const juce::MouseEvent& event) override
    {
        endDragAt(event.source.getScreenPosition().roundToInt());
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        if (dragging && cursorHidden) window_geometry::hideWindowDragCursor();
    }

private:
    void finishDrag()
    {
        if (! dragging) return;
        dragging = false;
        if (cursorHidden)
            window_geometry::setWindowDragActive(owner != nullptr ? *owner.getComponent()
                : static_cast<juce::Component&>(*this), false);
        cursorHidden = false;
    }

    juce::Point<int> dragPosition(const juce::Point<int> pointerPosition) const
    {
        const auto raw = startWindowPosition + pointerPosition - startPointerPosition;
        if (! movedDuringDrag) return raw;
        const auto snapped = window_geometry::snapPosition(
            { raw.x, raw.y, draggedWindowSize.x, draggedWindowSize.y }, snapTargets);
        return { snapped.x, snapped.y };
    }

    juce::Component::SafePointer<juce::Component> owner;
    juce::Point<int> startWindowPosition, startPointerPosition, draggedWindowSize;
    std::vector<window_geometry::SnapTarget> snapTargets;
    bool snappingEnabled = true;
    bool dragging = false, cursorHidden = false, movedDuringDrag = false;
};
