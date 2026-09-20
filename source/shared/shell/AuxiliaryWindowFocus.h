#pragma once

#include <JuceHeader.h>

#if JUCE_MAC
void setAuxiliaryTextInputActive(juce::Component& window, bool active,
                                 void*& previousKeyWindow);
#endif
