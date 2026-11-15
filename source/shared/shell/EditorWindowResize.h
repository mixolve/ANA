#pragma once

#include <JuceHeader.h>

// Keep the opposite native window edge fixed during an embedded editor resize.
// The host still controls the editor size; we never offset its child view.
class EditorWindowResize final
{
public:
    EditorWindowResize() = default;
    ~EditorWindowResize();
    EditorWindowResize(const EditorWindowResize&) = delete;
    EditorWindowResize& operator=(const EditorWindowResize&) = delete;
    void begin(juce::Component& editor, juce::ResizableEdgeComponent::Edge edge);
    // The bottom-right corner resizes both axes and keeps the native top-left
    // pinned, exactly like the right and bottom edges.
    void beginCorner(juce::Component& editor);
    void update();
    void end();

private:
    void beginResize(juce::Component& editor, bool fromLeft, bool fromTop);

    void* state = nullptr;
};
