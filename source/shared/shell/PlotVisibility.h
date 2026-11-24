#pragma once

#include <JuceHeader.h>

namespace ana::ui
{
// A temporary visibility override; do not rewrite the user's per-module settings.
inline void hidePlotControls(juce::Component& view)
{
    for (auto* child : view.getChildren())
        child->setVisible(false);
}
}
