#pragma once

#include <JuceHeader.h>
#include "WindowSnap.h"

namespace window_geometry
{
bool canResizeNativeWindow(juce::Component& component);
juce::Rectangle<int> getBounds(juce::Component& component);
void setPosition(juce::Component& component, juce::Point<int> position);
std::vector<SnapTarget> getSnapTargets(juce::Component& component);
void setWindowDragActive(juce::Component& component, bool active);
void hideWindowDragCursor();
// Size of the host window's content area, in editor units (zero when unknown).
juce::Point<int> getHostContentSize(juce::Component& component);
// A host may display the editor's native view scaled (for example fitted into
// a resized FX window). Restores an anisotropic (stretching) view mapping.
void resetViewContentsScale(juce::Component& component);
struct DragCursorImage
{
    juce::Image image;
    juce::Point<float> size, hotspot;
};
DragCursorImage getWindowDragCursorImage(juce::Component& component);
}
