#pragma once

#include <JuceHeader.h>

#if JUCE_MAC
void enableAuxiliaryMouseMoveEvents(juce::Component& window);
void matchAuxiliaryWindowLevel(juce::Component& window, juce::Component& owner);
void setAuxiliaryTextInputActive(juce::Component& window, bool active,
                                 void*& previousKeyWindow);
#endif
