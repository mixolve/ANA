#pragma once
#include "HostSlots.h"
#include "Theme.h"
#include "SquareControl.h"
class SlotIndicator final : public juce::Component, private juce::Timer
{
public:
    SlotIndicator() { setName("SL"); setWantsKeyboardFocus(false); }
    void bind(HostSlots* slotsIn,std::function<juce::String()> targetIn)
    {
        slots=slotsIn; target=std::move(targetIn);
        lastTarget.clear(); lastAssigned=false; lastHover=false;
        repaint();
        if (isShowing()) startTimerHz(10);
    }
    bool hasTarget() const { return slots && target && target().isNotEmpty(); }
    std::function<void()> onMark;
    void copyBindingFrom(const SlotIndicator& other) { bind(other.slots,other.target); }
    void layout(juce::Rectangle<int> bounds,bool show)
    {
        auto strip=bounds.reduced(4);
        strip.setTop(juce::jmax(strip.getY(),bounds.getBottom()-16));
        setBounds(strip); setVisible(show); toFront(false);
    }
    void visibilityChanged() override
    {
        if (isShowing()) startTimerHz(10);
        else { stopTimer(); hoverAt=0; pressed=marking=false; lastHover=false; }
    }
    void mouseEnter(const juce::MouseEvent&) override
    {
        hoverAt=juce::Time::getMillisecondCounterHiRes()+50.0;
        // Run briefly at 30 Hz only for the 50 ms hover transition.
        fastHoverTimer=true;
        startTimerHz(30);
        repaint();
    }
    void mouseExit(const juce::MouseEvent&) override
    {
        hoverAt=0; lastHover=false;
        fastHoverTimer=false;
        if (isShowing()) startTimerHz(10);
        repaint();
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        marking=e.mods.isCtrlDown() && !e.mods.isShiftDown() && !e.mods.isAltDown() && !e.mods.isCommandDown() && onMark!=nullptr;
        pressed=!marking && e.mods.isLeftButtonDown() && hasTarget(); repaint();
    }
    void mouseUp(const juce::MouseEvent& e) override
    {
        const bool clicked=getLocalBounds().contains(e.getPosition()) && !e.mouseWasDraggedSinceMouseDown();
        if(std::exchange(marking,false)) { if(clicked && onMark) onMark(); }
        else if(std::exchange(pressed,false) && clicked && hasTarget()) slots->toggle(target());
        repaint();
    }
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        // A child SL must not trap Settings/Dock wheel scrolling.
        if (auto* viewport = findParentComponentOfClass<juce::Viewport>())
            viewport->mouseWheelMove(e.getEventRelativeTo(viewport), wheel);
        else
            juce::Component::mouseWheelMove(e, wheel);
    }
    void paint(juce::Graphics& g) override
    {
        const auto id=target?target():juce::String(); const bool available=slots && id.isNotEmpty();
        const bool assigned=available && slots->assigned(id);
        const bool hover=available && isMouseOver() && !ana::ui::square::hoverBlocked() && hoverAt>0 && juce::Time::getMillisecondCounterHiRes()>=hoverAt;
        g.fillAll(!available?juce::Colour(0xff888888):pressed?ana::ui::white:hover?juce::Colour(0xff666666):assigned?ana::ui::white:ana::ui::light);
        drawEmbeddedButtonText(g,"SL",getLocalBounds(),hover && !pressed?ana::ui::white:ana::ui::background);
    }
private:
    HostSlots* slots=nullptr;
    std::function<juce::String()> target;
    bool pressed=false,marking=false;
    double hoverAt=0;
    // Assignment and hover changes invalidate the slot; the timer need not repaint unconditionally.
    juce::String lastTarget;
    bool lastAssigned=false,lastHover=false,fastHoverTimer=false;
    static bool drawEmbeddedButtonText(juce::Graphics& g,const juce::String& text,juce::Rectangle<int> bounds, juce::Colour colour)
{
    if (bounds.isEmpty()) return false;
    g.setColour(colour);
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText(ana::ui::makeFont(), text.trim(), 0.0f, 0.0f);
    juce::Path ink;
    glyphs.createPath(ink);
    const auto shape = ink.getBounds();
    if (!shape.isEmpty())
    {
        const auto zone = bounds.reduced(1).toFloat();
        juce::GlyphArrangement reference;
        reference.addLineOfText(ana::ui::makeFont(), "H", 0.0f, 0.0f);
        juce::Path capInk;
        reference.createPath(capInk);
        const auto cap = capInk.getBounds();
        const auto scale = zone.getHeight() / cap.getHeight();
        const auto width = shape.getWidth() * scale;
        const auto overflow = width > zone.getWidth();
        const auto period = width + 8.0f;
        const auto offset = overflow ? float(std::fmod(juce::Time::getMillisecondCounterHiRes() * 0.024, double(period))) : 0.0f;
        const auto x = overflow ? zone.getX() - offset : zone.getCentreX() - width * 0.5f;
        const auto y = zone.getCentreY() - cap.getCentreY() * scale;
        const juce::Graphics::ScopedSaveState saved(g);
        g.reduceClipRegion(bounds.reduced(1));
        g.fillPath(ink, juce::AffineTransform::scale(scale).translated(x - shape.getX() * scale, y));
        if (overflow)
        {
            g.fillPath(ink, juce::AffineTransform::scale(scale).translated(x + period - shape.getX() * scale, y));
            return true;
        }
    }
    return false;
}
    void timerCallback() override
    {
        if (!isShowing()) { stopTimer(); return; }
        const auto id=target?target():juce::String();
        const bool assigned=slots && id.isNotEmpty() && slots->assigned(id);
        const auto now=juce::Time::getMillisecondCounterHiRes();
        const bool hover=id.isNotEmpty() && hoverAt>0.0 && now>=hoverAt
            && isMouseOver() && !ana::ui::square::hoverBlocked();
        if (id!=lastTarget || assigned!=lastAssigned || hover!=lastHover)
        {
            lastTarget=id; lastAssigned=assigned; lastHover=hover;
            repaint();
        }
        if (fastHoverTimer && (hoverAt<=0.0 || now>=hoverAt))
        {
            fastHoverTimer=false;
            startTimerHz(10);
        }
    }
};
