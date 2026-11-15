#include <juce_opengl/juce_opengl.h>
#import <Cocoa/Cocoa.h>
#include "EditorWindowResize.h"

namespace
{
struct WindowResizeState
{
    WindowResizeState(NSWindow* target, const bool leftIn, const bool topIn)
        : window([target retain]), originalFrame(target.frame), left(leftIn), top(topIn)
    {
        auto* resizeState = this;
        resizeObserver = [[NSNotificationCenter.defaultCenter
            addObserverForName:NSWindowDidResizeNotification object:window queue:nil
            usingBlock:^(NSNotification*) { resizeState->update(); }] retain];
        moveObserver = [[NSNotificationCenter.defaultCenter
            addObserverForName:NSWindowDidMoveNotification object:window queue:nil
            usingBlock:^(NSNotification*) { resizeState->update(); }] retain];
    }
    ~WindowResizeState()
    {
        [NSNotificationCenter.defaultCenter removeObserver:resizeObserver];
        [NSNotificationCenter.defaultCenter removeObserver:moveObserver];
        [resizeObserver release];
        [moveObserver release];
        [window release];
    }
    void update()
    {
        if (updating)
            return;
        auto origin = window.frame.origin;
        origin.x = left ? NSMaxX(originalFrame) - window.frame.size.width : NSMinX(originalFrame);
        // Cocoa coordinates: top resize preserves bottom; others preserve top.
        origin.y = top ? NSMinY(originalFrame) : NSMaxY(originalFrame) - window.frame.size.height;
        if (std::abs(origin.x - window.frame.origin.x) < 0.25
            && std::abs(origin.y - window.frame.origin.y) < 0.25)
            return;
        updating = true;
        [window setFrameOrigin:origin];
        updating = false;
    }
    NSWindow* window;
    NSRect originalFrame;
    bool left, top, updating = false;
    id resizeObserver = nil, moveObserver = nil;
};
}

void EditorWindowResize::beginResize(juce::Component& editor, const bool fromLeft, const bool fromTop)
{
    end();
    auto* view = static_cast<NSView*>(editor.getWindowHandle());
    auto* window = view.window;
    if (window == nil || (window.styleMask & NSWindowStyleMaskFullScreen) != 0)
        return;
    const auto editorBounds = editor.getScreenBounds();
    const auto contentBounds = window.contentView.bounds;
    // A docked editor must not move the entire DAW's main window.
    if (editorBounds.getWidth() < contentBounds.size.width * 0.65
        || editorBounds.getHeight() < contentBounds.size.height * 0.65)
        return;
    state = new WindowResizeState(window, fromLeft, fromTop);
}

void EditorWindowResize::update()
{
    if (auto* resizeState = static_cast<WindowResizeState*>(state))
        resizeState->update();
}

void EditorWindowResize::end()
{
    delete static_cast<WindowResizeState*>(state);
    state = nullptr;
}
