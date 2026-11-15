#include <juce_opengl/juce_opengl.h>
#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>

#include "WindowGeometry.h"

namespace window_geometry
{
namespace
{
NSWindow* getWindow(juce::Component& component)
{
    auto* view = static_cast<NSView*>(component.getWindowHandle());
    return view != nil ? [view window] : nil;
}

CGFloat desktopTop()
{
    return NSMaxY([[[NSScreen screens] firstObject] frame]);
}
}

bool canResizeNativeWindow(juce::Component& component)
{
    auto* window = getWindow(component);
    if (window == nil || (window.styleMask & NSWindowStyleMaskFullScreen) != 0) return false;
    if (dynamic_cast<juce::AudioProcessorEditor*>(&component) == nullptr) return true;
    const auto editor = component.getScreenBounds();
    const auto content = window.contentView.bounds;
    return editor.getWidth() >= content.size.width * 0.65
        && editor.getHeight() >= content.size.height * 0.65;
}

juce::Point<int> getHostContentSize(juce::Component& component)
{
    if (auto* window = getWindow(component))
    {
        const auto content = window.contentView.bounds;
        return { juce::roundToInt(content.size.width), juce::roundToInt(content.size.height) };
    }
    return {};
}

void resetViewContentsScale(juce::Component& component)
{
    auto* view = static_cast<NSView*>(component.getWindowHandle());
    if (view == nil) return;

    const auto frameSize = view.frame.size;
    const auto boundsSize = view.bounds.size;
    if (frameSize.width < 1.0 || frameSize.height < 1.0
        || boundsSize.width < 1.0 || boundsSize.height < 1.0)
        return;

    const auto scaleX = frameSize.width / boundsSize.width;
    const auto scaleY = frameSize.height / boundsSize.height;

    // A uniform view scale is a deliberate host UI scaling. Only a stretched
    // (anisotropic) view distorts the editor, so only that is restored.
    if (std::abs(scaleX - scaleY) <= 0.01 * std::max(scaleX, scaleY))
        return;

    [view setBoundsSize:frameSize];
}

juce::Rectangle<int> getBounds(juce::Component& component)
{
    if (auto* window = getWindow(component))
    {
        const auto frame = [window frame];
        return { juce::roundToInt(frame.origin.x), juce::roundToInt(desktopTop() - NSMaxY(frame)),
                 juce::roundToInt(frame.size.width), juce::roundToInt(frame.size.height) };
    }
    return component.getTopLevelComponent()->getScreenBounds();
}

void setPosition(juce::Component& component, juce::Point<int> position)
{
    if (auto* window = getWindow(component))
        [window setFrameTopLeftPoint:NSMakePoint(position.x, desktopTop() - position.y)];
}

std::vector<SnapTarget> getSnapTargets(juce::Component& component)
{
    std::vector<SnapTarget> targets;
    for (NSScreen* screen in [NSScreen screens])
    {
        const auto frame = screen.visibleFrame;
        targets.push_back({ { juce::roundToInt(frame.origin.x),
            juce::roundToInt(desktopTop() - NSMaxY(frame)),
            juce::roundToInt(frame.size.width), juce::roundToInt(frame.size.height) }, true });
    }
    auto* ownWindow = getWindow(component);
    const auto ownId = ownWindow != nil ? static_cast<CGWindowID>(ownWindow.windowNumber) : kCGNullWindowID;
    // Cache geometry once per drag; never capture window images or inspect titles.
    auto windows = CGWindowListCopyWindowInfo(
        kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements, kCGNullWindowID);
    if (windows == nullptr) return targets;
    for (NSDictionary* info in static_cast<NSArray*>(windows))
    {
        const auto key = [](CFStringRef value) { return static_cast<NSString*>(value); };
        const auto layer = [info[key(kCGWindowLayer)] intValue];
        if ([info[key(kCGWindowNumber)] unsignedIntValue] == ownId
            || layer < NSNormalWindowLevel || layer >= NSStatusWindowLevel
            || [info[key(kCGWindowAlpha)] doubleValue] <= 0.0)
            continue;
        CGRect frame {};
        if (CGRectMakeWithDictionaryRepresentation(static_cast<CFDictionaryRef>(info[key(kCGWindowBounds)]), &frame)
            && frame.size.width > 1 && frame.size.height > 1)
            targets.push_back({ { juce::roundToInt(frame.origin.x), juce::roundToInt(frame.origin.y),
                juce::roundToInt(frame.size.width), juce::roundToInt(frame.size.height) }, false });
    }
    CFRelease(windows);
    return targets;
}

void hideWindowDragCursor()
{
    juce::Desktop::getInstance().getMainMouseSource().hideCursor();
}

void setWindowDragActive(juce::Component& component, const bool active)
{
    if (auto* window = getWindow(component))
    {
        if (active) [window disableCursorRects];
        else [window enableCursorRects];
    }
    if (active) { [NSCursor hide]; hideWindowDragCursor(); }
    else { [NSCursor unhide]; juce::Desktop::getInstance().getMainMouseSource().forceMouseCursorUpdate(); }
}


DragCursorImage getWindowDragCursorImage(juce::Component&)
{
    auto* cursor = [NSCursor openHandCursor];
    auto* nativeImage = [cursor image];
    auto* representation = [nativeImage TIFFRepresentation];
    if (representation == nil) return {};
    auto* bitmap = [NSBitmapImageRep imageRepWithData:representation];
    if (bitmap == nil) return {};
    auto* data = [bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
    if (data == nil) return {};
    const auto size = [nativeImage size];
    const auto hotspot = [cursor hotSpot];
    return { juce::ImageFileFormat::loadFrom([data bytes], static_cast<size_t>([data length])),
        { static_cast<float>(size.width), static_cast<float>(size.height) },
        { static_cast<float>(hotspot.x), static_cast<float>(hotspot.y) } };
}

}
