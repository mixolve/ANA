#pragma once
#include "HostSlots.h"
#include "Theme.h"
#include "SquareControl.h"

class SlotsPanel final : public juce::Component, private juce::Timer
{
public:
    explicit SlotsPanel(HostSlots& slotsIn):slots(slotsIn)
    { setWantsKeyboardFocus(true); for(int i=0;i<HostSlots::count;++i) { rows[size_t(i)]=std::make_unique<Row>(*this,i); addAndMakeVisible(*rows[size_t(i)]); } startTimerHz(15); }
    void resized() override { for(int i=0;i<HostSlots::count;++i) rows[size_t(i)]->setBounds(0,i*88,getWidth(),80); }
    static constexpr int contentHeight=64*80+63*8;
    void cancelMove() { beginDragAutoRepeat(0); source=destination=-1; repaint(); }
    void mouseDown(const juce::MouseEvent&) override { cancelMove(); }
    bool keyPressed(const juce::KeyPress& key) override
    { if(key.isKeyCode(juce::KeyPress::escapeKey) || key.isKeyCode(juce::KeyPress::returnKey)) { cancelMove(); return true; } return false; }
private:
    HostSlots& slots; int source=-1,destination=-1;
    struct Row final : juce::Component
    {
        SlotsPanel& owner; int index; double flashUntil=0; bool down=false,dragged=false,marqueePending=false; int pressedCell=-1;
        Row(SlotsPanel& ownerIn,int i):owner(ownerIn),index(i) { setName("host-slot-"+juce::String(i+1)); setMouseCursor(juce::MouseCursor::NormalCursor); }
        bool hitTest(int x,int y) override { return x>=0 && x<getWidth() && y>=0 && y<getHeight() && (x<80 || x>=88); }
        juce::MouseCursor getMouseCursor() override { return owner.source>=0?juce::MouseCursor(juce::MouseCursor::DraggingHandCursor):juce::MouseCursor(juce::MouseCursor::NormalCursor); }
        void paint(juce::Graphics& g) override
        {
            const auto target=owner.slots.targetId(index);
            const bool moving=owner.source==index || owner.destination==index;
            const bool enabled=target.isNotEmpty() || owner.source>=0;
            const bool flash=enabled && juce::Time::getMillisecondCounterHiRes()<flashUntil;
            auto number=getLocalBounds().withWidth(80), label=getLocalBounds().withTrimmedLeft(88);
            for(auto r:{number,label})
            {
                g.setColour(ana::ui::dark); g.fillRect(r);
                g.setColour(ana::ui::light); g.drawRect(r,1);
            }
            const auto numberText=number.reduced(4), labelText=label.reduced(4);
            const auto normal=enabled?ana::ui::light:juce::Colour(0xff888888);
            const bool numberHeld=enabled && down && !dragged && pressedCell==0;
            const bool labelHeld=enabled && down && !dragged && pressedCell==1;
            g.setColour(numberHeld?ana::ui::white:moving?ana::ui::square::panelBackground:normal); g.fillRect(numberText);
            g.setColour(moving && !numberHeld?ana::ui::white:ana::ui::background);
            if(ana::ui::square::drawCaption(g,juce::String::formatted("%02d-",index+1)+HostSlots::letter(index),numberText)) ana::ui::scheduleMarqueeRepaint(*this,marqueePending);
            g.setColour(flash || labelHeld?ana::ui::white:moving?ana::ui::square::panelBackground:normal); g.fillRect(labelText);
            g.setColour(moving && !flash && !labelHeld?ana::ui::white:ana::ui::background); g.setFont(ana::ui::makeFont());
            if(ana::ui::drawLoopingText(g,owner.slots.displayName(index).toUpperCase(),labelText.reduced(8,0),ana::ui::makeFont(),juce::Justification::centredLeft)) ana::ui::scheduleMarqueeRepaint(*this,marqueePending);
        }
        void mouseDown(const juce::MouseEvent& e) override
        {
            if(owner.slots.targetId(index).isEmpty() && owner.source<0) return;
            down=e.mods.isLeftButtonDown() || e.mods.isCtrlDown(); dragged=false;
            pressedCell=e.position.x<80?0:1; repaint();
            owner.grabKeyboardFocus();
        }
        void mouseDrag(const juce::MouseEvent& e) override
        {
            if(!down || e.mods.isCtrlDown() || e.getDistanceFromDragStart()<4 || owner.slots.targetId(index).isEmpty()) return;
            if(!dragged) { dragged=true; owner.source=index; beginDragAutoRepeat(30); }
            const auto screen=e.source.getScreenPosition();
            if(auto* viewport=owner.findParentComponentOfClass<juce::Viewport>())
            {
                auto vp=viewport->getLocalPoint(nullptr,screen);
                viewport->autoScroll(juce::roundToInt(vp.x),juce::roundToInt(vp.y),16,8);
                if(!viewport->getLocalBounds().toFloat().contains(vp)) { owner.destination=-1; owner.repaint(); return; }
            }
            const auto position=owner.getLocalPoint(nullptr,screen);
            const auto candidate=int(position.y)/88;
            owner.destination=owner.getLocalBounds().toFloat().contains(position) && int(position.y)%88<80 && juce::isPositiveAndBelow(candidate,HostSlots::count)?candidate:-1;
            owner.repaint();
        }
        void mouseUp(const juce::MouseEvent& e) override
        {
            if(!std::exchange(down,false)) return;
            pressedCell=-1; repaint();
            if(dragged)
            {
                beginDragAutoRepeat(0);
                const auto screen=e.source.getScreenPosition(); const auto position=owner.getLocalPoint(nullptr,screen);
                bool inside=owner.getLocalBounds().toFloat().contains(position);
                if(auto* viewport=owner.findParentComponentOfClass<juce::Viewport>()) inside=inside && viewport->getLocalBounds().toFloat().contains(viewport->getLocalPoint(nullptr,screen));
                const auto candidate=int(position.y)/88;
                if(inside && int(position.y)%88<80 && owner.source>=0 && juce::isPositiveAndBelow(candidate,HostSlots::count)) owner.slots.swap(owner.source,candidate);
                owner.cancelMove(); return;
            }
            if(!getLocalBounds().contains(e.getPosition()) || e.mouseWasDraggedSinceMouseDown()) return;
            if(e.mods.isCtrlDown() && e.mods.isShiftDown() && !e.mods.isAltDown()) owner.slots.clear(index);
            else if(e.mods.isCtrlDown() && !e.mods.isShiftDown() && !e.mods.isAltDown())
            { const auto id=owner.slots.targetId(index); if(id.isNotEmpty()) { juce::SystemClipboard::copyTextToClipboard("/ana/"+id); flashUntil=juce::Time::getMillisecondCounterHiRes()+120; } }
            owner.repaint();
        }
    };
    std::array<std::unique_ptr<Row>,HostSlots::count> rows;
    std::array<juce::String, HostSlots::count> lastTargets, lastNames;
    void timerCallback() override
    {
        if (!isShowing()) return;
        const auto now=juce::Time::getMillisecondCounterHiRes();
        for (int i=0;i<HostSlots::count;++i)
        {
            const auto index=static_cast<size_t>(i);
            const auto target=slots.targetId(i);
            const auto name=slots.displayName(i);
            if (target!=lastTargets[index] || name!=lastNames[index])
            {
                lastTargets[index]=target;
                lastNames[index]=name;
                rows[index]->repaint();
            }
            if (rows[index]->flashUntil>0.0 && now>=rows[index]->flashUntil)
            {
                rows[index]->flashUntil=0.0;
                rows[index]->repaint();
            }
        }
    }
};
