#pragma once

#include "Theme.h"
#include "ValueRange.h"
#include "PointerInput.h"
#include <limits>
#include <optional>

namespace ana::ui::square
{
inline constexpr int size = 80;
inline constexpr int spacing = 8;
inline constexpr int columns = 6;
inline constexpr int edgeInset = 4;
inline constexpr int innerGap = 2;
inline constexpr int indicatorSize = 12;
inline constexpr int contentWidth = columns * size + (columns - 1) * spacing;
inline constexpr int windowWidth = contentWidth + 4 * spacing + 40;
inline const juce::Colour panelBackground { 0xff666666 };
inline const juce::Colour disabled { 0xff888888 };
inline int activeValueDrags=0;
inline bool hoverBlocked()
{ return activeValueDrags>0 || juce::Desktop::getInstance().getNumDraggingMouseSources()>0; }

inline juce::Rectangle<int> rail(juce::Rectangle<int> bounds)
{
    return bounds.reduced(edgeInset).withHeight(indicatorSize);
}

inline juce::Rectangle<int> textBounds(juce::Rectangle<int> bounds, bool hasRail)
{
    auto text = bounds.reduced(edgeInset);
    if (hasRail) text.removeFromTop(indicatorSize + innerGap);
    return text;
}

inline bool drawCaption(juce::Graphics& g, const juce::String& source, juce::Rectangle<int> bounds)
{
    const juce::Graphics::ScopedSaveState saved(g);
    if (bounds.isEmpty() || !g.reduceClipRegion(bounds)) return false;
    auto text = uppercaseText(source);
    if (text.indexOfChar('-') > 0) text = text.replaceCharacter('-', ' ');
    // SCOP's L TO R is explicitly a one-line caption, including in an 80px tile.
    auto font = makeFont();
    const auto lineHeight = juce::roundToInt(font.getHeight());
    const auto words = juce::StringArray::fromTokens(text, " ", "");
    if (text != "L TO R" && !text.containsChar('\n') && bounds.getHeight() >= 2 * lineHeight && words.size() > 1
        && (source.containsChar('-') || getTextPixelWidth(font, text) > bounds.getWidth()))
    {
        int splitAt = 1, bestWidth = std::numeric_limits<int>::max();
        for (int split = 1; split < words.size(); ++split)
        {
            const auto width = juce::jmax(getTextPixelWidth(font, words.joinIntoString(" ", 0, split)),
                getTextPixelWidth(font, words.joinIntoString(" ", split, words.size() - split)));
            if (width < bestWidth) { bestWidth = width; splitAt = split; }
        }
        text = words.joinIntoString(" ", 0, splitAt) + "\n"
            + words.joinIntoString(" ", splitAt, words.size() - splitAt);
    }
    const auto lines = juce::StringArray::fromLines(text);
    const auto count = juce::jmin(2, lines.size());
    auto y = bounds.getCentreY() - count * lineHeight / 2;
    bool scrolling = false;
    g.setFont(font);
    for (int line = 0; line < count; ++line)
    {
        scrolling = drawLoopingText(g, lines[line], bounds.withY(y).withHeight(lineHeight), font) || scrolling;
        y += lineHeight;
    }
    return scrolling;
}

inline void paintBody(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    g.setColour(dark); g.fillRect(bounds);
    g.setColour(light); g.drawRect(bounds, 1);
}

inline void paintTextButton(juce::Graphics& g, juce::Rectangle<int> bounds,
                            bool hovering, bool pressed, bool enabled)
{
    g.setColour(!enabled ? disabled : pressed ? white : hovering ? panelBackground : light);
    g.fillRect(bounds);
    g.setColour(enabled && hovering && !pressed ? white : background);
}

inline juce::Rectangle<int> marker(juce::Rectangle<int> bounds, double position)
{
    const auto track = rail(bounds);
    const auto x = track.getX() + juce::roundToInt((track.getWidth() - track.getHeight())
        * juce::jlimit(0.0, 1.0, position));
    return { x, track.getY(), track.getHeight(), track.getHeight() };
}

inline bool pointerOver(const juce::Component& component, juce::Rectangle<int> bounds)
{
    if(hoverBlocked() || !component.isShowing() || juce::Component::getCurrentlyModalComponent()!=nullptr) return false;
    // Native coordinates cannot drift into neighbouring controls with JUCE's
    // hidden, unbounded cursor. Geometry is queried afresh, including side edges.
    const auto point=component.getLocalPoint(nullptr,juce::Desktop::getMousePosition().toFloat());
    if(!bounds.toFloat().contains(point)) return false;
    auto* root=component.getTopLevelComponent();
    auto* hit=root->getComponentAt(root->getLocalPoint(&component,point).toInt());
    return hit==&component || (hit!=nullptr && component.isParentOf(hit));
}

inline void paintRail(juce::Graphics& g, juce::Rectangle<int> bounds, double position, bool enabled,
                      bool interacting = false, bool hovered = false)
{
    const auto track = rail(bounds);
    g.setColour(light);
    g.fillRect(track.toFloat().withY(float(track.getCentreY()) - 0.5f).withHeight(1.0f));
    g.setColour(!enabled ? disabled : interacting ? white : hovered ? panelBackground : light);
    g.fillRect(marker(bounds, position));
}

// Lock to one drag axis; Shift fine-adjusts using the same travel scale.
class Drag final
{
public:
    ~Drag() { end(false); }
    void begin(juce::Slider& slider, int count, double unitStep = 0.0)
    {
        end(false); target = &slider; choices = count;
        position = ValueRange::rangePosition(slider);
        dragUnitStep = unitStep; startValue = slider.getValue(); unitTravel = 0.0;
        distance = 0.0; axis = 0; moved = false;
    }
    bool active() const { return target != nullptr; }
    bool wasDragged() const { return moved; }
    double visualPosition() const { return position; }
    void drag(const juce::MouseEvent& event, int travel)
    {
        if (!active()) return;
        const auto offset = event.position - event.mouseDownPosition;
        if (axis == 0)
        {
            if (juce::jmax(std::abs(offset.x), std::abs(offset.y)) < 3.0f) return;
            axis = std::abs(offset.x) >= std::abs(offset.y) ? 1 : 2;
        }
        const auto next = axis == 1 ? double(offset.x) : -double(offset.y);
        const auto previousDistance = distance;
        const auto increment = (next - distance) * (event.mods.isShiftDown() ? 0.1 : 1.0)
            / (2.0 * juce::jmax(1, travel));
        distance = next;
        if (!moved)
        {
            moved = true;
            ++activeValueDrags; hoverLock=true; dragComponent=event.eventComponent;
            if(dragComponent!=nullptr) dragComponent->getTopLevelComponent()->repaint();
            if(event.eventComponent!=nullptr && event.eventComponent->isShowing())
            {
                mouse = event.source;
                startPosition = event.getMouseDownScreenPosition().toFloat();
                unbounded = mouse->canDoUnboundedMovement() && !mouse->isUnboundedMouseMovementEnabled();
                if (unbounded) mouse->enableUnboundedMouseMovement(true);
                mouse->hideCursor();
            }
            gesture = std::make_unique<juce::Slider::ScopedDragNotification>(*target);
        }
        if (choices > 2)
        {
            const auto period = double(choices) / double(choices - 1);
            position = std::fmod(position + increment, period);
            if (position < 0.0) position += period;
        }
        else position = juce::jlimit(0.0, 1.0, position + increment);
        if (choices > 1)
        {
            const auto index=juce::roundToInt(position*(choices-1))%choices;
            target->setValue(target->proportionOfLengthToValue(double(index)/(choices-1)),juce::sendNotificationSync);
        }
        else if (dragUnitStep > 0.0)
        {
            // Fine-adjusted numeric parameters (SLOPE): the configured unit
            // step is applied directly; the host parameter supplies quantisation.
            unitTravel += (next - previousDistance) * (event.mods.isShiftDown() ? 0.1 : 1.0);
            target->setValue(startValue + unitTravel * dragUnitStep, juce::sendNotificationSync);
            position = ValueRange::rangePosition(*target);
        }
        else target->setValue(ValueRange::valueAt(*target, position), juce::sendNotificationSync);
    }
    void end(bool commit)
    {
        if (commit && target != nullptr && moved && choices > 1)
        {
            const auto index = juce::roundToInt(position * (choices - 1)) % choices;
            target->setValue(target->proportionOfLengthToValue(double(index) / (choices - 1)), juce::sendNotificationSync);
        }
        gesture.reset(); target = nullptr;
        if (mouse.has_value())
        {
            if (unbounded) mouse->enableUnboundedMouseMovement(false);
            mouse->setScreenPosition(startPosition);
            mouse->revealCursor(); mouse->forceMouseCursorUpdate(); mouse.reset();
        }
        // Keep other controls inert until the real cursor is restored.
        if(hoverLock) { --activeValueDrags; hoverLock=false; }
        if(dragComponent!=nullptr) dragComponent->getTopLevelComponent()->repaint();
        dragComponent=nullptr;
    }
private:
    juce::Component::SafePointer<juce::Slider> target;
    std::unique_ptr<juce::Slider::ScopedDragNotification> gesture;
    double position = 0.0, distance = 0.0;
    double dragUnitStep = 0.0, startValue = 0.0, unitTravel = 0.0;
    int axis = 0, choices = 0;
    bool moved = false, unbounded = false, hoverLock=false;
    juce::Component::SafePointer<juce::Component> dragComponent;
    std::optional<juce::MouseInputSource> mouse;
    juce::Point<float> startPosition;
};
}
