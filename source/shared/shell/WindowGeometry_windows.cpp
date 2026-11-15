#include "WindowGeometry.h"

#if JUCE_WINDOWS
#include <windows.h>
#include <vector>

namespace window_geometry
{
namespace
{
int dragCursorHideCalls = 0;

HWND getWindow(juce::Component& component)
{
    auto* view = static_cast<HWND>(component.getWindowHandle());
    return view != nullptr ? GetAncestor(view, GA_ROOT) : nullptr;
}
}

bool canResizeNativeWindow(juce::Component& component)
{
    if (dynamic_cast<juce::AudioProcessorEditor*>(&component) == nullptr) return true;
    auto* window = getWindow(component);
    RECT content {};
    if (window == nullptr || IsZoomed(window) || !GetClientRect(window, &content)) return false;
    const auto logical = juce::Desktop::getInstance().getDisplays().physicalToLogical(
        juce::Rectangle<int>(content.left, content.top, content.right-content.left, content.bottom-content.top));
    const auto editor = component.getScreenBounds();
    return editor.getWidth() >= logical.getWidth() * 0.65 && editor.getHeight() >= logical.getHeight() * 0.65;
}

juce::Point<int> getHostContentSize(juce::Component& component)
{
    auto* window = getWindow(component);
    RECT content {};
    if (window == nullptr || ! GetClientRect(window, &content)) return {};
    const auto logical = juce::Desktop::getInstance().getDisplays().physicalToLogical(
        juce::Rectangle<int>(content.left, content.top, content.right-content.left, content.bottom-content.top));
    return { logical.getWidth(), logical.getHeight() };
}

void resetViewContentsScale(juce::Component&) {}

juce::Rectangle<int> getBounds(juce::Component& component)
{
    RECT bounds {};
    if (auto* window = getWindow(component); window != nullptr && GetWindowRect(window, &bounds))
        return juce::Desktop::getInstance().getDisplays().physicalToLogical(
            juce::Rectangle<int>(bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top));
    return component.getTopLevelComponent()->getScreenBounds();
}

void setPosition(juce::Component& component, juce::Point<int> position)
{
    if (auto* window = getWindow(component))
    {
        const auto physical = juce::Desktop::getInstance().getDisplays().logicalToPhysical(position);
        SetWindowPos(window, nullptr, physical.x, physical.y, 0, 0,
                     SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER);
    }
}
std::vector<SnapTarget> getSnapTargets(juce::Component& component)
{
    std::vector<SnapTarget> targets;
    for (const auto& display : juce::Desktop::getInstance().getDisplays().displays)
    {
        const auto area = display.userArea;
        targets.push_back({ { area.getX(), area.getY(), area.getWidth(), area.getHeight() }, true });
    }
    struct Context { HWND own; std::vector<SnapTarget>& targets; } context { getWindow(component), targets };
    EnumWindows([](HWND window, LPARAM data) -> BOOL
    {
        auto& state = *reinterpret_cast<Context*>(data);
        if (window == state.own || !IsWindowVisible(window) || IsIconic(window)) return TRUE;
        RECT frame {};
        if (GetWindowRect(window, &frame) && frame.right > frame.left && frame.bottom > frame.top)
        {
            const auto area = juce::Desktop::getInstance().getDisplays().physicalToLogical(
                juce::Rectangle<int>(frame.left, frame.top, frame.right - frame.left, frame.bottom - frame.top));
            state.targets.push_back({ { area.getX(), area.getY(), area.getWidth(), area.getHeight() }, false });
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&context));
    return targets;
}

void hideWindowDragCursor()
{
    auto source = juce::Desktop::getInstance().getMainMouseSource();
    source.hideCursor();
}
void setWindowDragActive(juce::Component&, const bool active)
{
    if (active)
    {
        if (dragCursorHideCalls == 0)
        {
            int visibility;
            do { visibility = ShowCursor(FALSE); ++dragCursorHideCalls; } while (visibility >= 0);
        }
        hideWindowDragCursor();
    }
    else
    {
        while (dragCursorHideCalls > 0) { ShowCursor(TRUE); --dragCursorHideCalls; }
        juce::Desktop::getInstance().getMainMouseSource().forceMouseCursorUpdate();
    }
}

DragCursorImage getWindowDragCursorImage(juce::Component& component)
{
    const auto cursor = GetCursor();
    ICONINFO info {};
    if (cursor == nullptr || !GetIconInfo(cursor, &info)) return {};
    BITMAP bitmap {};
    GetObject(info.hbmColor != nullptr ? info.hbmColor : info.hbmMask, sizeof(bitmap), &bitmap);
    const int width = bitmap.bmWidth;
    const int height = info.hbmColor != nullptr ? bitmap.bmHeight : bitmap.bmHeight / 2;
    if (info.hbmColor != nullptr) DeleteObject(info.hbmColor);
    if (info.hbmMask != nullptr) DeleteObject(info.hbmMask);
    if (width <= 0 || height <= 0) return {};
    auto dc = CreateCompatibleDC(nullptr);
    if (dc == nullptr) return {};
    BITMAPINFO header {};
    header.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    header.bmiHeader.biWidth = width; header.bmiHeader.biHeight = -height;
    header.bmiHeader.biPlanes = 1; header.bmiHeader.biBitCount = 32;
    header.bmiHeader.biCompression = BI_RGB;
    void* pixels = nullptr;
    auto dib = CreateDIBSection(dc, &header, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (dib == nullptr) { DeleteDC(dc); return {}; }
    const auto old = SelectObject(dc, dib);
    auto* data = static_cast<juce::uint32*>(pixels);
    const auto count = static_cast<size_t>(width) * static_cast<size_t>(height);
    std::fill(data, data + count, 0u);
    DrawIconEx(dc, 0, 0, cursor, width, height, 0, nullptr, DI_NORMAL);
    GdiFlush();
    const std::vector<juce::uint32> black(data, data + count);
    std::fill(data, data + count, 0x00ffffffu);
    DrawIconEx(dc, 0, 0, cursor, width, height, 0, nullptr, DI_NORMAL);
    GdiFlush();
    juce::Image image(juce::Image::ARGB, width, height, true);
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x)
    {
        const auto index = static_cast<size_t>(y * width + x);
        const auto channel = [](juce::uint32 pixel, int shift) { return int((pixel >> shift) & 255u); };
        const int alpha = juce::jlimit(0, 255, 255 - (channel(data[index], 16) - channel(black[index], 16)));
        const auto straight = [&](int shift) { return static_cast<juce::uint8>(alpha > 0 ? juce::jlimit(0, 255, channel(black[index], shift) * 255 / alpha) : 0); };
        image.setPixelAt(x, y, juce::Colour::fromRGBA(straight(16), straight(8), straight(0), static_cast<juce::uint8>(alpha)));
    }
    SelectObject(dc, old); DeleteObject(dib); DeleteDC(dc);
    const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(component.getScreenBounds());
    const float scale = display != nullptr ? static_cast<float>(display->scale) : 1.0f;
    return { image, { float(width) / scale, float(height) / scale }, { float(info.xHotspot) / scale, float(info.yHotspot) / scale } };
}

}
#elif ! JUCE_MAC
namespace window_geometry
{
bool canResizeNativeWindow(juce::Component& component) { return component.getTopLevelComponent()->isOnDesktop(); }

juce::Point<int> getHostContentSize(juce::Component&) { return {}; }
void resetViewContentsScale(juce::Component&) {}

juce::Rectangle<int> getBounds(juce::Component& component)
{
    return component.getTopLevelComponent()->getScreenBounds();
}
void setPosition(juce::Component& component, juce::Point<int> position)
{
    auto* top = component.getTopLevelComponent();
    if (top->isOnDesktop())
        top->setTopLeftPosition(position);
}
std::vector<SnapTarget> getSnapTargets(juce::Component&)
{
    std::vector<SnapTarget> targets;
    for (const auto& display : juce::Desktop::getInstance().getDisplays().displays)
    {
        const auto area = display.userArea;
        targets.push_back({ { area.getX(), area.getY(), area.getWidth(), area.getHeight() }, true });
    }
    return targets;
}
void hideWindowDragCursor()
{
    auto source = juce::Desktop::getInstance().getMainMouseSource();
    source.hideCursor();
}
void setWindowDragActive(juce::Component&, const bool active)
{
    if (active) hideWindowDragCursor();
    else juce::Desktop::getInstance().getMainMouseSource().forceMouseCursorUpdate();
}

DragCursorImage getWindowDragCursorImage(juce::Component&) { return {}; }

}
#endif
