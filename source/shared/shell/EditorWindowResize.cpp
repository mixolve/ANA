#include "EditorWindowResize.h"
#include "WindowGeometry.h"

EditorWindowResize::~EditorWindowResize() { end(); }
void EditorWindowResize::begin(juce::Component& editor, const juce::ResizableEdgeComponent::Edge edge)
{
    beginResize(editor,
        edge == juce::ResizableEdgeComponent::leftEdge,
        edge == juce::ResizableEdgeComponent::topEdge);
}

void EditorWindowResize::beginCorner(juce::Component& editor)
{
    beginResize(editor, false, false);
}


#if !JUCE_MAC
#if JUCE_WINDOWS
#include <windows.h>
namespace
{
struct WindowResizeState
{
    HWND window = nullptr;
    RECT original {};
    bool left = false, top = false;
};
}
#endif

void EditorWindowResize::beginResize(juce::Component& editor, const bool fromLeft, const bool fromTop)
{
    end();
#if JUCE_WINDOWS
    const auto window = GetAncestor(static_cast<HWND>(editor.getWindowHandle()), GA_ROOT);
    RECT frame {};
    if (window == nullptr || !window_geometry::canResizeNativeWindow(editor)
        || !GetWindowRect(window, &frame))
        return;
    state = new WindowResizeState { window, frame, fromLeft, fromTop };
#else
    juce::ignoreUnused(editor, fromLeft, fromTop);
#endif
}
void EditorWindowResize::update()
{
#if JUCE_WINDOWS
    if (auto* resizeState = static_cast<WindowResizeState*>(state))
    {
        RECT frame {};
        if (!GetWindowRect(resizeState->window, &frame))
            return;
        const auto x = resizeState->left ? resizeState->original.right - (frame.right - frame.left) : resizeState->original.left;
        const auto y = resizeState->top ? resizeState->original.bottom - (frame.bottom - frame.top) : resizeState->original.top;
        if (x != frame.left || y != frame.top)
            SetWindowPos(resizeState->window, nullptr, x, y, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
#endif
}
void EditorWindowResize::end()
{
#if JUCE_WINDOWS
    delete static_cast<WindowResizeState*>(state);
#endif
    state = nullptr;
}
#endif
