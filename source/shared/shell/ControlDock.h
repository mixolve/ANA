#pragma once
#include "SettingsPanel.h"
#include "Processor.h"
#include <map>

// AVA dock controls: sort at the top, collapse in the middle, orientation below.
class AnaDockToggle final : public juce::Button
{
public:
    AnaDockToggle() : juce::Button("dock-toggle") { setClickingTogglesState(true); setWantsKeyboardFocus(false); }
    void paintButton(juce::Graphics& g,bool hovered,bool held) override
    { g.fillAll(held?ana::ui::white:hovered?ana::ui::dark:getToggleState()?ana::ui::white:ana::ui::light); }
};
class ControlDock final : public juce::Component, private juce::Timer
{
public:
    ControlDock(PluginProcessor& p, SettingsPanel& s) : processor(p), settings(s)
    {
        getProperties().set("emptyEditorBackground",true);
        viewport.onWheel=[this](const juce::MouseWheelDetails& w) { scroll(w); };
        addAndMakeVisible(viewport); viewport.setViewedComponent(&content,false);
        viewport.setScrollBarsShown(false,false); viewport.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::never);
        for(auto* b:{&sort,&collapse,&orientation}) addAndMakeVisible(*b);
        sort.setTooltip("SORT DOCK"); collapse.setTooltip("COLLAPSE TO DOCK"); orientation.setTooltip("VERTICAL DOCK");
        sort.onClick=[this] { overlay.setVisible(sort.getToggleState()); overlay.toFront(false); };
        collapse.onClick=[this] { state().setProperty("ana.dock.only",collapse.getToggleState(),nullptr); if(onModeChanged) onModeChanged(); };
        orientation.onClick=[this] { state().setProperty("ana.dock.vertical",orientation.getToggleState(),nullptr); if(onModeChanged) onModeChanged(); };
        addChildComponent(overlay); overlay.owner=this;
        startTimerHz(15); refresh();
    }
    ~ControlDock() override { viewport.setViewedComponent(nullptr,false); }
    std::function<void()> onModeChanged;
    bool dockOnly() const { return bool(processor.getParameters().state.getProperty("ana.dock.only",false)); }
    bool vertical() const { return dockOnly() && bool(processor.getParameters().state.getProperty("ana.dock.vertical",false)); }
    juce::Point<int> collapsedSize() const
    {
        const int length=processor.getCollapsedDockCount()*88-8;
        return vertical()?juce::Point<int>(96,length+44):juce::Point<int>(length+36,104);
    }
    void paint(juce::Graphics& g) override { g.fillAll(ana::ui::windowBackground); }
    void resized() override
    {
    const auto gap = 8;
    const auto inset = gap;
    const auto side = 80;
    const auto height = side + 2 * inset;
    const bool vertical = dockOnly() && bool(processor.getParameters().state.getProperty("ana.dock.vertical", false));
    // Use the horizontal dock as the single layout reference. In vertical
    // mode every rectangle is rotated clockwise.
    const auto layoutWidth = vertical ? getHeight() : getWidth();
    const auto layoutHeight = vertical ? side + 3 * gap : getHeight();
    const auto rotate = [vertical, layoutHeight](juce::Rectangle<int> r)
    {
        return vertical ? juce::Rectangle<int>(layoutHeight - r.getBottom(), r.getX(), r.getHeight(), r.getWidth()) : r;
    };
    const auto toggleSize = 12;
    // The configured capacity, independent of the number of pinned controls.
    const int count=dockOnly()?processor.getCollapsedDockCount():6;
    const auto visibleLength=count*side+(count-1)*gap;
    const auto expandedStripX = 2 * (gap + 2);
    const auto stripX = vertical ? gap : (dockOnly() ? 0 : expandedStripX);
    const juce::Rectangle<int> strip(stripX, layoutHeight - height,
        visibleLength + 2 * inset, height);
    const auto dockBounds = strip.reduced(inset);
    const auto rightButtonsX = dockOnly()
        ? dockBounds.getRight() + gap
        : layoutWidth - gap - toggleSize;
    viewport.setBounds(rotate(dockBounds));
    // Only three service controls: SORT, COLLAPSE, ORIENTATION.
    sort.setBounds(rotate({rightButtonsX, dockBounds.getY(), toggleSize, toggleSize}));
    collapse.setBounds(rotate({rightButtonsX, dockBounds.getCentreY() - toggleSize,
                               toggleSize, 2 * toggleSize}));
    orientation.setBounds(rotate({rightButtonsX, dockBounds.getBottom() - toggleSize,
                                  toggleSize, toggleSize}));
    orientation.setEnabled(dockOnly());
    overlay.setBounds(rotate(dockBounds));
    const auto r=rotate(dockBounds);
        content.setSize(vertical?80:juce::jmax(r.getWidth(),int(items.size())*88-8),vertical?juce::jmax(r.getHeight(),int(items.size())*88-8):80);
        for(size_t i=0;i<items.size();++i) items[i].copy->setBounds(vertical?0:int(i)*88,vertical?int(i)*88:0,80,80);
    }
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override { scroll(wheel); }
private:
    struct Item { juce::String key; juce::Component::SafePointer<juce::Component> source; std::unique_ptr<juce::Component> copy; };
    PluginProcessor& processor; SettingsPanel& settings;
    class DockViewport : public juce::Viewport
    { public: std::function<void(const juce::MouseWheelDetails&)> onWheel;
      void mouseWheelMove(const juce::MouseEvent&,const juce::MouseWheelDetails& w) override { if(onWheel) onWheel(w); }
    };
    juce::Component content; DockViewport viewport;
    AnaDockToggle sort,collapse,orientation;
    std::vector<Item> items; juce::String signature; int dragging=-1;
    int lastCount=-1;
    struct Overlay : juce::Component
    {
        ControlDock* owner=nullptr;
        void mouseDown(const juce::MouseEvent& e) override { if(e.mods.isLeftButtonDown()) { owner->drag(e,0); beginDragAutoRepeat(40); } }
        void mouseDrag(const juce::MouseEvent& e) override { if(e.getDistanceFromDragStart()>=4) owner->drag(e,1); }
        void mouseUp(const juce::MouseEvent& e) override { beginDragAutoRepeat(0); owner->drag(e,2); }
        void mouseWheelMove(const juce::MouseEvent&,const juce::MouseWheelDetails& w) override { owner->scroll(w); }
    } overlay;
    juce::ValueTree& state() { return processor.getParameters().state; }
    void scroll(const juce::MouseWheelDetails& w)
    {
        const int delta=juce::roundToInt((std::abs(w.deltaX)>std::abs(w.deltaY)?w.deltaX:w.deltaY)*-(w.isSmooth?140.f:70.f));
        viewport.setViewPosition(vertical()?0:viewport.getViewPositionX()+delta,vertical()?viewport.getViewPositionY()+delta:0);
    }
    void drag(const juce::MouseEvent& e,int phase)
    {
        auto axis=vertical()?e.position.y:e.position.x;
        const int position=vertical()?viewport.getViewPositionY():viewport.getViewPositionX();
        const int index=juce::jlimit(0,juce::jmax(0,int(items.size())-1),int(axis+float(position))/88);
        if(phase==0) dragging=items.empty()?-1:index;
        if(dragging<0) return;
        if(phase==1)
        {
            const auto extent=vertical()?overlay.getHeight():overlay.getWidth();
            if(axis<8 || axis>float(extent-8)) viewport.setViewPosition(vertical()?0:position+(axis<8?-8:8),vertical()?position+(axis<8?-8:8):0);
            if(index>dragging) std::rotate(items.begin()+dragging,items.begin()+dragging+1,items.begin()+index+1);
            else if(index<dragging) std::rotate(items.begin()+index,items.begin()+dragging,items.begin()+dragging+1);
            dragging=index; resized();
        }
        if(phase==2) { juce::StringArray order; for(auto& item:items) order.add(item.key); state().setProperty("ana.dock.order",order.joinIntoString("|"),nullptr); dragging=-1; signature.clear(); }
    }
    void timerCallback() override { refresh(); }
    void refresh()
    {
        const int count=processor.getCollapsedDockCount();
        if(count!=lastCount)
        {
            lastCount=count;
            if(dockOnly()) { if(onModeChanged) onModeChanged(); else resized(); }
        }
        std::map<juce::String,juce::Component*> selected;
        for(auto* c:settings.dockSources())
        {
            if (!bool(c->getProperties().getWithDefault(juce::Identifier("dockAllowed"), true)))
                continue;
            bool marked=false;
            if(auto* p=dynamic_cast<ParameterControl*>(c)) { for(auto* child:p->getChildren()) if(auto* n=dynamic_cast<NumericValueBox*>(child)) marked=n->isMarked && n->isMarked(); }
            else if(auto* b=dynamic_cast<ControlButton*>(c)) marked=b->isMarked && b->isMarked();
            else if(auto* n=dynamic_cast<NumericValueBox*>(c)) marked=n->isMarked && n->isMarked();
            const auto key=c->getProperties()["dockSelectionKey"].toString();
            if(marked && key.isNotEmpty()) selected[key]=c;
        }
        auto order=juce::StringArray::fromTokens(state().getProperty("ana.dock.order").toString(),"|","");
        for(auto& entry:selected) if(!order.contains(entry.first)) order.add(entry.first);
        for(int i=order.size();--i>=0;) if(selected.find(order[i])==selected.end()) order.remove(i);
        const auto next=order.joinIntoString("|");
        if(signature!=next)
        {
            items.clear(); signature=next; state().setProperty("ana.dock.order",next,nullptr);
            for(auto& key:order)
            {
                auto* source=selected[key]; std::unique_ptr<juce::Component> copy;
                if(auto* p=dynamic_cast<ParameterControl*>(source))
                {
                    auto c=p->createDockCopy();
                    c->setMarking({}, {});
                    c->setSlotIndicatorVisible(false);
                    copy=std::move(c);
                }
                else if(auto* n=dynamic_cast<NumericValueBox*>(source))
                {
                    auto c=n->createDockCopy();
                    c->onMark=nullptr;
                    c->isMarked=nullptr;
                    c->setSlotIndicatorVisible(false);
                    copy=std::move(c);
                }
                else if(auto* b=dynamic_cast<ControlButton*>(source))
                {
                    auto c=std::make_unique<ControlButton>(b->getButtonText()); c->setSquareStyle(true); c->valueTextProvider=b->valueTextProvider; c->setSlotIndicatorVisible(false);
                    c->onClick=[safe=juce::Component::SafePointer<ControlButton>(b)] { if(safe && safe->isInteractionEnabled()) { if(safe->getClickingTogglesState()) safe->setToggleState(!safe->getToggleState(),juce::dontSendNotification); if(safe->onClick) safe->onClick(); } }; copy=std::move(c);
                }
                if(copy) { content.addAndMakeVisible(*copy); items.push_back({key,source,std::move(copy)}); }
            }
            resized();
        }
        for(auto& item:items) if(item.source)
        {
            // Keep the copied control enabled; the SL area is not present in the dock.
            item.copy->setEnabled(true);
            const bool sourceEnabled = item.source->isEnabled();
            if(auto* b=dynamic_cast<ControlButton*>(item.copy.get())) if(auto* original=dynamic_cast<ControlButton*>(item.source.getComponent())) { b->setToggleState(original->getToggleState(),juce::dontSendNotification); b->setInteractionEnabled(sourceEnabled && original->isInteractionEnabled()); }
            if(auto* p=dynamic_cast<ParameterControl*>(item.source.getComponent())) if(auto* c=dynamic_cast<ParameterControl*>(item.copy.get())) { p->syncDockCopy(*c); if(!sourceEnabled) c->setInteractionEnabled(false, true); }
            if(auto* n=dynamic_cast<NumericValueBox*>(item.source.getComponent())) if(auto* c=dynamic_cast<NumericValueBox*>(item.copy.get())) c->setInteractionEnabled(sourceEnabled && n->isInteractionEnabled());
        }
        collapse.setToggleState(dockOnly(),juce::dontSendNotification); orientation.setToggleState(vertical(),juce::dontSendNotification);
    }
};
