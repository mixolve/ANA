#pragma once

#include <JuceHeader.h>
#include "WindowGeometry.h"

class EdgeResizeHandle : public juce::Component
{
public:
    enum class Axis
    {
        horizontal,
        vertical,
        both
    };

    EdgeResizeHandle(juce::Component& ownerIn, const Axis axisIn,
                     const int minimumSizeIn, const int maximumSizeIn,
                     const bool leadingEdgeIn = false, const bool snapToEdges = true)
        : owner(&ownerIn),
          axis(axisIn), minimumSize(minimumSizeIn), maximumSize(maximumSizeIn),
          leadingEdge(leadingEdgeIn), snappingEnabled(snapToEdges)
    {
        setMouseCursor(axis == Axis::both ? juce::MouseCursor::BottomRightCornerResizeCursor
            : axis == Axis::horizontal ? juce::MouseCursor::LeftRightResizeCursor
                                       : juce::MouseCursor::UpDownResizeCursor);
        setWantsKeyboardFocus(false);
        setMouseClickGrabsKeyboardFocus(false);
    }


    void mouseDown(const juce::MouseEvent& event) override
    {
        dragStartBounds = owner->getBounds();
        const auto screen = owner->getScreenBounds();
        scaleX = double(screen.getWidth()) / juce::jmax(1, dragStartBounds.getWidth());
        scaleY = double(screen.getHeight()) / juce::jmax(1, dragStartBounds.getHeight());
        nativeResizeAllowed = window_geometry::canResizeNativeWindow(*owner);
        dragStartWindowBounds = nativeResizeAllowed ? window_geometry::getBounds(*owner) : screen;
        snapTargets = nativeResizeAllowed && snappingEnabled ? window_geometry::getSnapTargets(*owner)
                                         : std::vector<window_geometry::SnapTarget>{};
        if (const auto* display = juce::Desktop::getInstance().getDisplays()
                                      .getDisplayForRect(dragStartWindowBounds))
            screenBounds = display->userArea;
        else
            screenBounds = dragStartWindowBounds;
        dragStartScreenPosition = event.source.getScreenPosition().roundToInt();
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        const auto horizontal = axis == Axis::horizontal;
        // The handle moves during layout. Use the input source position rather than
        // converting event-local coordinates through its changing bounds.
        const auto physicalDistance = event.source.getScreenPosition().roundToInt() - dragStartScreenPosition;
        const juce::Point<int> distance { juce::roundToInt(physicalDistance.x / scaleX),
                                         juce::roundToInt(physicalDistance.y / scaleY) };
        auto proposed = getProposedBounds(distance);
        if (auto* editor = dynamic_cast<juce::AudioProcessorEditor*>(owner))
            if (auto* constrainer = editor->getConstrainer())
                constrainer->checkBounds(proposed, owner->getBounds(), screenBounds,
                    axis != Axis::horizontal && leadingEdge, axis != Axis::vertical && leadingEdge,
                    axis != Axis::horizontal && !leadingEdge, axis != Axis::vertical && !leadingEdge);
        if (axis == Axis::both)
        {
            // Use the same one-axis host resize requests as the side handles.
            // Some hosts stretch the embedded view when both dimensions change
            // in a single request. Preserve the accepted width on the second pass.
            owner->setSize(proposed.getWidth(), owner->getHeight());
            owner->setSize(owner->getWidth(), proposed.getHeight());
        }
        else
        {
            owner->setSize(proposed.getWidth(), proposed.getHeight());
        }

        if (leadingEdge && nativeResizeAllowed)
        {
            const auto actualSize = horizontal ? owner->getWidth() : owner->getHeight();
            const auto startSize = horizontal ? dragStartBounds.getWidth() : dragStartBounds.getHeight();
            const auto offset = juce::roundToInt((startSize - actualSize) * (horizontal ? scaleX : scaleY));
            window_geometry::setPosition(*owner, dragStartWindowBounds.getPosition()
                + (horizontal ? juce::Point<int>(offset, 0) : juce::Point<int>(0, offset)));
        }
    }

private:
    juce::ComponentBoundsConstrainer* getConstrainer() const
    {
        if (auto* editor = dynamic_cast<juce::AudioProcessorEditor*>(owner)) return editor->getConstrainer();
        if (auto* window = dynamic_cast<juce::ResizableWindow*>(owner)) return window->getConstrainer();
        return nullptr;
    }
    int getMinimum(bool horizontal) const
    {
        if (auto* c = getConstrainer())
                return horizontal ? c->getMinimumWidth() : c->getMinimumHeight();
        return minimumSize;
    }
    int getSizeLimit(const bool horizontal) const
    {
        const auto startSize = horizontal ? dragStartBounds.getWidth() : dragStartBounds.getHeight();
        const auto availableSize = leadingEdge
            ? juce::roundToInt((horizontal ? dragStartWindowBounds.getX() - screenBounds.getX()
                          : dragStartWindowBounds.getY() - screenBounds.getY()) / (horizontal ? scaleX : scaleY)) + startSize
            : juce::roundToInt((horizontal ? screenBounds.getRight() - dragStartWindowBounds.getRight()
                          : screenBounds.getBottom() - dragStartWindowBounds.getBottom()) / (horizontal ? scaleX : scaleY)) + startSize;
        auto maximum = maximumSize;
        if (auto* c = getConstrainer())
                maximum = horizontal ? c->getMaximumWidth() : c->getMaximumHeight();
        return juce::jmax(1, juce::jmin(maximum, availableSize));
    }

    juce::Rectangle<int> getProposedBounds(const juce::Point<int> distance) const
    {
        const auto resizedDimension = [this, distance](const bool horizontal)
        {
            const auto startSize = horizontal ? dragStartBounds.getWidth() : dragStartBounds.getHeight();
            const auto delta = horizontal ? distance.x : distance.y;
            const auto limit = getSizeLimit(horizontal);
            return juce::jlimit(juce::jmin(getMinimum(horizontal), limit), limit,
                startSize + (leadingEdge ? -delta : delta));
        };
        const auto width = axis == Axis::vertical ? owner->getWidth() : resizedDimension(true);
        const auto height = axis == Axis::horizontal ? owner->getHeight() : resizedDimension(false);
        // Targets describe native window frames, including any host title bar.
        const auto frameWidth = dragStartWindowBounds.getWidth() - juce::roundToInt(dragStartBounds.getWidth() * scaleX);
        const auto frameHeight = dragStartWindowBounds.getHeight() - juce::roundToInt(dragStartBounds.getHeight() * scaleY);
        const auto physicalWidth = juce::roundToInt(width * scaleX);
        const auto physicalHeight = juce::roundToInt(height * scaleY);
        const window_geometry::SnapBounds outer {
            leadingEdge && axis != Axis::vertical ? dragStartWindowBounds.getRight() - physicalWidth - frameWidth
                                                  : dragStartWindowBounds.getX(),
            leadingEdge && axis != Axis::horizontal ? dragStartWindowBounds.getBottom() - physicalHeight - frameHeight
                                                    : dragStartWindowBounds.getY(),
            physicalWidth + frameWidth, physicalHeight + frameHeight };
        const auto widthLimit = getSizeLimit(true), heightLimit = getSizeLimit(false);
        const auto snapped = window_geometry::snapResize(outer, snapTargets,
            { leadingEdge && axis != Axis::vertical, leadingEdge && axis != Axis::horizontal,
              !leadingEdge && axis != Axis::vertical, !leadingEdge && axis != Axis::horizontal },
            { frameWidth + juce::roundToInt(juce::jmin(getMinimum(true), widthLimit) * scaleX),
              frameHeight + juce::roundToInt(juce::jmin(getMinimum(false), heightLimit) * scaleY),
              frameWidth + juce::roundToInt(widthLimit * scaleX), frameHeight + juce::roundToInt(heightLimit * scaleY) });
        return owner->getBounds().withSize(juce::roundToInt((snapped.width - frameWidth) / scaleX), juce::roundToInt((snapped.height - frameHeight) / scaleY));
    }

    juce::Component* owner;
    bool nativeResizeAllowed = false;
    double scaleX = 1.0, scaleY = 1.0;
    Axis axis;
    const int minimumSize;
    const int maximumSize;
    const bool leadingEdge;
    const bool snappingEnabled;
    juce::Rectangle<int> dragStartBounds;
    juce::Rectangle<int> dragStartWindowBounds;
    juce::Rectangle<int> screenBounds;
    juce::Point<int> dragStartScreenPosition;
    std::vector<window_geometry::SnapTarget> snapTargets;
};
