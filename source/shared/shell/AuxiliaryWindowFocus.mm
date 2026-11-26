
#include <juce_opengl/juce_opengl.h>
#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>
#include "AuxiliaryWindowFocus.h"

// Auxiliary peers stay visible when the host loses focus. Only their level is
// lowered, so a host's floating FX level cannot leak above other applications.
@interface ANAAuxiliaryActivation : NSObject
{
    NSWindow* auxiliaryWindow;
    NSInteger hostLevel;
    BOOL pinned;
}
- (instancetype)initWithWindow:(NSWindow*)window;
- (void)setHostLevel:(NSInteger)level;
- (void)setPinned:(BOOL)shouldPin;
- (void)activationChanged:(NSNotification*)notification;
@end

@implementation ANAAuxiliaryActivation
- (instancetype)initWithWindow:(NSWindow*)window
{
    self = [super init];
    if (self != nil)
    {
        auxiliaryWindow = window; // Owned by the associated window, not retained.
        hostLevel = window.level;
        auto* notifications = [NSNotificationCenter defaultCenter];
        [notifications addObserver:self selector:@selector(activationChanged:)
                              name:NSApplicationDidBecomeActiveNotification object:NSApp];
        [notifications addObserver:self selector:@selector(activationChanged:)
                              name:NSApplicationDidResignActiveNotification object:NSApp];
    }
    return self;
}
- (void)setHostLevel:(NSInteger)level
{
    pinned = NO;
    hostLevel = level;
    auxiliaryWindow.level = NSApp.active ? hostLevel : NSNormalWindowLevel;
}
- (void)setPinned:(BOOL)shouldPin
{
    pinned = shouldPin;
    auxiliaryWindow.level = pinned ? NSFloatingWindowLevel
                                   : (NSApp.active ? hostLevel : NSNormalWindowLevel);
}
- (void)activationChanged:(NSNotification*)notification
{
    if (!pinned)
        auxiliaryWindow.level = [notification.name isEqualToString:NSApplicationDidBecomeActiveNotification]
            ? hostLevel : NSNormalWindowLevel;
}
- (void)dealloc
{
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    [super dealloc];
}
@end

namespace
{
char auxiliaryActivationKey;
ANAAuxiliaryActivation* activationFor(NSWindow* window)
{
    auto* activation = static_cast<ANAAuxiliaryActivation*>(objc_getAssociatedObject(window, &auxiliaryActivationKey));
    if (activation == nil)
    {
        activation = [[ANAAuxiliaryActivation alloc] initWithWindow:window];
        objc_setAssociatedObject(window, &auxiliaryActivationKey, activation, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        [activation release];
    }
    return activation;
}
NSWindow* nativeWindowFor(juce::Component& component)
{
    auto* view = static_cast<NSView*>(component.getWindowHandle());
    return view != nil ? view.window : nil;
}
}

void enableAuxiliaryMouseMoveEvents(juce::Component& component)
{
    if (auto* window = nativeWindowFor(component))
    {
        [window setAcceptsMouseMovedEvents:YES];
        // REAPER may restore auxiliary windows through its native screenset
        // machinery. Explicitly restore the window's interactive hit-test
        // state as well as mouse-move delivery (used by hover animations).
        [window setIgnoresMouseEvents:NO];
        [window setHidesOnDeactivate:NO];
        activationFor(window);
    }
}

void matchAuxiliaryWindowLevel(juce::Component& component, juce::Component& owner)
{
    auto* window = nativeWindowFor(component);
    auto* ownerWindow = nativeWindowFor(owner);
    if (window == nil || ownerWindow == nil || window == ownerWindow)
        return;

    if (auto* oldParent = window.parentWindow)
        [oldParent removeChildWindow:window];
    [activationFor(window) setHostLevel:ownerWindow.level];
    window.hidesOnDeactivate = NO;
}

void attachAuxiliaryWindowToOwner(juce::Component& component, juce::Component& owner)
{
    auto* window = nativeWindowFor(component);
    if (window == nil)
        return;

    if (component.isAlwaysOnTop())
    {
        detachAuxiliaryWindowFromOwner(component);
        [activationFor(window) setPinned:YES];
        window.hidesOnDeactivate = NO;
        [window orderFrontRegardless];
        return;
    }

    auto* ownerWindow = nativeWindowFor(owner);
    if (ownerWindow == nil || window == ownerWindow)
    {
        [activationFor(window) setPinned:NO];
        return;
    }

    if (window.parentWindow != ownerWindow)
    {
        if (auto* oldParent = window.parentWindow)
            [oldParent removeChildWindow:window];
        [ownerWindow addChildWindow:window ordered:NSWindowAbove];
    }
    // Screenset activation can reorder the host's native windows after JUCE
    // has shown the dock. Reassert the child z-order without making the dock
    // a key window or stealing REAPER's keyboard shortcuts.
    [window orderFront:nil];
    // Sit just above the host's normal/floating utility windows. A native
    // transparent host overlay at the owner's level can otherwise receive
    // hit-tests while the dock remains visible beneath it.
    const NSInteger dockLevel = ownerWindow.level < NSModalPanelWindowLevel - 1
        ? ownerWindow.level + 1 : ownerWindow.level;
    [activationFor(window) setHostLevel:dockLevel];
    window.hidesOnDeactivate = NO;
}

void detachAuxiliaryWindowFromOwner(juce::Component& component)
{
    if (auto* window = nativeWindowFor(component))
        if (auto* parent = window.parentWindow)
            [parent removeChildWindow:window];
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

        component.getProperties().set("mixolveAuxiliaryTextInputActive", true);
    }
    else
    {
        component.getProperties().set("mixolveAuxiliaryTextInputActive", false);

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
