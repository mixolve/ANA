#import <Cocoa/Cocoa.h>

#include "AuxiliaryWindowFocus.h"

void setAuxiliaryTextInputActive(juce::Component& component, const bool active,
                                 void*& previousKeyWindow)
{
    auto* view = static_cast<NSView*>(component.getWindowHandle());
    auto* window = view.window;

    if (active)
    {
        auto* currentKeyWindow = NSApp.keyWindow;
        if (currentKeyWindow != nil && currentKeyWindow != window && previousKeyWindow == nullptr)
            previousKeyWindow = [currentKeyWindow retain];

        component.getProperties().set("anaAuxiliaryTextInputActive", true);
    }
    else
    {
        component.getProperties().set("anaAuxiliaryTextInputActive", false);

        auto* previous = static_cast<NSWindow*>(previousKeyWindow);
        if (previous != nil)
        {
            if (previous.visible && previous != window)
                [previous makeKeyWindow];
            [previous release];
            previousKeyWindow = nullptr;
        }
    }
}
