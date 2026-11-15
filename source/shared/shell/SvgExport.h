#pragma once
#include <JuceHeader.h>
#include <functional>

namespace ana::svg_export
{
// Supplies a virtual canvas layout without resizing the native window.
class Layout
{
public:
    virtual ~Layout() = default;
    /** Canvas height that holds all of the content; 0 keeps the current size. */
    virtual int getSvgExportCanvasHeight() const { return 0; }
    /** Lays the content out for that canvas height right before it is painted. */
    virtual void prepareSvgExportLayout(int) {}
    /** Returns the content to the on-screen layout right after the paint. */
    virtual void finishSvgExportLayout() {}
};

juce::String render(juce::Component& component);
void chooseFile(juce::Component& component);
#if JUCE_MAC
void* installKeyboardShortcut(juce::Component& component, std::function<void()> onSquareResize = {});
void removeKeyboardShortcut(void* monitor);
#endif
}
