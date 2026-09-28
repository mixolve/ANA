#import <Cocoa/Cocoa.h>

#include "AuxiliaryWindowFocus.h"

namespace
{
NSWindow* nativeWindowFor(juce::Component& component)
{
    auto* view = static_cast<NSView*>(component.getWindowHandle());
    return view != nil ? view.window : nil;
}
}

void enableAuxiliaryMouseMoveEvents(juce::Component& component)
{
    if (auto* window = nativeWindowFor(component))
        [window setAcceptsMouseMovedEvents:YES];
}

void matchAuxiliaryWindowLevel(juce::Component& component, juce::Component& owner)
{
    auto* window = nativeWindowFor(component);
    auto* ownerWindow = nativeWindowFor(owner);
    if (window == nil || ownerWindow == nil || window == ownerWindow)
        return;

    if (auto* oldParent = window.parentWindow)
        [oldParent removeChildWindow:window];
    window.level = ownerWindow.level;
}

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
