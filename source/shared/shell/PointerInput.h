#pragma once

#include <JuceHeader.h>
#include <typeinfo>

namespace ana::ui
{
inline bool isMouseHovering(const juce::Component& component)
{
    const auto& desktop=juce::Desktop::getInstance();
    if (desktop.getNumDraggingMouseSources()>0) return false;
    for (const auto& source:desktop.getMouseSources())
        if (source.isMouse() && const_cast<juce::Component&>(component).reallyContains(
            component.getLocalPoint(nullptr,source.getScreenPosition()),true)) return true;
    return false;
}

inline bool isEmptyEditorBackground(const juce::Component* component)
{
    return component!=nullptr && (component==component->getTopLevelComponent()
        || typeid(*component)==typeid(juce::Component)
        || dynamic_cast<const juce::Viewport*>(component)!=nullptr
        || bool(component->getProperties()["emptyEditorBackground"]));
}

inline juce::Component* componentAtPointer(juce::Component& component)
{
    auto* root=component.getTopLevelComponent();
    return root->getComponentAt(root->getLocalPoint(nullptr,juce::Desktop::getMousePosition()));
}
}
