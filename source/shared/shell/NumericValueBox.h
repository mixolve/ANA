#pragma once
#include "ValueEditor.h"
#include "NumericTextRaster.h"
#include "SquareControl.h"
#include "SlotIndicator.h"

class NumericValueBox final : public juce::Component, private juce::Slider::Listener, private juce::Timer
{
public:
    explicit NumericValueBox(juce::Slider& slider) : target(slider)
    {
        addChildComponent(slotIndicator);
        slotIndicator.onMark=[this] { if(onMark) onMark(); };
        setWantsKeyboardFocus(false);
        setMouseClickGrabsKeyboardFocus(false);
        target.addListener(this);
        juce::Desktop::getInstance().addGlobalMouseListener(&external);
    }
    ~NumericValueBox() override
    {
        juce::Desktop::getInstance().removeGlobalMouseListener(&external);
        target.removeListener(this);
        hideEditor(true);
    }
    void setSquareStyle(std::function<juce::String()> title)
    { idleText = std::move(title); resized(); repaint(); }
    void bindHostSlots(HostSlots* slots,std::function<juce::String()> targetId) { slotIndicator.bind(slots,std::move(targetId)); }
    std::unique_ptr<NumericValueBox> createDockCopy()
    {
        auto copy=std::make_unique<NumericValueBox>(target);
        copy->bottomSlotVisible=bottomSlotVisible;
        copy->slotIndicator.copyBindingFrom(slotIndicator);
        copy->setSquareStyle(idleText); copy->setDiscreteChoiceCount(choiceCount);
        copy->setDragUnitStep(dragUnitStep);
        copy->setNumericControlEnabled(numeric); copy->displayTextProvider=displayTextProvider;
        copy->onClick=onClick; copy->onChoiceClicked=onChoiceClicked;
        copy->onDiscreteEdited=onDiscreteEdited; copy->onResetRequested=onResetRequested;
        return copy;
    }
    void setSlotIndicatorVisible(bool visible) { bottomSlotVisible=visible; resized(); repaint(); }
    void setDiscreteChoiceCount(int count) { choiceCount=count; resized(); repaint(); }
    void setDragUnitStep(double step) { dragUnitStep = step; }
    std::function<void()> onInteractionBegin;
    std::function<void(int)> onChoiceClicked;
    std::function<void()> onResetRequested;
    std::function<bool()> isMarked;
    std::function<void()> onMark;
    bool isSquare() const { return idleText != nullptr; }
    bool isDiscreteGestureActive() const noexcept { return squareDrag.active() || pressed; }
    void cancelGesture() { squareDrag.end(false); direct.end(); pressed=false; marking=false; }
    void setNumericControlEnabled(bool enabled) { numeric=enabled; resized(); repaint(); }
    // Disabling the parameter must not disable its independent SL assignment button.
    void setInteractionEnabled(bool enabled)
    {
        // Dock copies are synchronized frequently; a no-op update must not
        // repaint or restart editing/hover state on every timer tick.
        if (interactive == enabled) return;
        if (!enabled) { cancelGesture(); hideEditor(true); }
        interactive=enabled;
        repaint();
    }
    bool isInteractionEnabled() const noexcept { return interactive; }
    void paint(juce::Graphics& g) override
    {
        if (isSquare()) { ana::ui::square::paintBody(g,getLocalBounds()); paintSquare(g);
            if(isMarked && isMarked()) { g.setColour(ana::ui::white); g.drawRect(getLocalBounds(),1); } return; }
        const auto editing = editor != nullptr;
        const auto hoverAmount = editing ? 0.0f : hover.amount(interactive && ana::ui::isMouseHovering(*this));
        ControlAppearance(false, hoverAmount, editing).paintBackground(g, getLocalBounds());
    }
    void paintOverChildren(juce::Graphics& g) override
    {
        if (isSquare()) return;
        const auto editing=editor!=nullptr;
        ControlAppearance style(false,editing?0.0f:hover.amount(interactive && ana::ui::isMouseHovering(*this)),editing);
        style.paintFrame(g,getLocalBounds());
        if(editor) return;
        const auto text=uppercaseText(displayTextProvider?displayTextProvider():target.getTextFromValue(target.getValue()));
        const auto textBounds=getLocalBounds().reduced(ana::ui::gap.pixels(),0);
        const auto elapsed=juce::Time::getMillisecondCounterHiRes()*0.001;
        const auto scrolling=textRaster.prepare(text,getLocalBounds(),textBounds,
            g.getInternalContext().getPhysicalPixelScaleFactor(),elapsed);
        g.setFont(ana::ui::makeFont()); g.setColour(interactive?style.contentColour():ana::ui::light);
        textRaster.draw(g);
        if(scrolling) ana::ui::scheduleMarqueeRepaint(*this,marqueePending);
    }
    void resized() override
    {
        slotIndicator.layout(getLocalBounds(),isSquare() && bottomSlotVisible);
        if(editor)
        {
            if(isSquare())
            {
                // Use the exact same caption rectangle as paintSquare(). The
                // bottom slot occupies 14 px, and centring the editor in that
                // area otherwise moves the text, selection and caret down.
                const auto text=squareCaptionBounds();
                editor->setBorder(juce::BorderSize<int>(text.getY(),text.getX(),
                    getHeight()-text.getBottom(),getWidth()-text.getRight()));
                editor->setBounds(getLocalBounds());
            }
            else ana::ui::alignInlineEditorText(*editor,getLocalBounds(),
                getLocalBounds().getCentreY()+ana::ui::readoutVerticalOffset());
        }
    }
    void visibilityChanged() override
    { if(!isShowing()) { cancelGesture(); hideEditor(true); stopTimer(); valueVisible=false; mouseInside=false; } }
    void enablementChanged() override { setInteractionEnabled(isEnabled()); }
    void mouseEnter(const juce::MouseEvent&) override
    { if(isSquare()) { mouseInside=!ana::ui::square::hoverBlocked(); transitionAt=juce::Time::getMillisecondCounterHiRes()+50.0; startTimer(10); } repaint(); }
    void mouseMove(const juce::MouseEvent& event) override
    { if(isSquare()) { mouseInside=!ana::ui::square::hoverBlocked() && getLocalBounds().toFloat().contains(event.position); if(!isTimerRunning()) startTimer(10); repaint(); } }
    void mouseExit(const juce::MouseEvent&) override
    { if(isSquare()) { mouseInside=false; transitionAt=juce::Time::getMillisecondCounterHiRes()+50.0; startTimer(10); }
      else pressed=false; repaint(); }
    void mouseDown(const juce::MouseEvent& event) override
    {
        if(isSquare() && onMark && event.originalComponent==this && event.mods.isCtrlDown()
            && !event.mods.isShiftDown() && !event.mods.isAltDown() && !event.mods.isCommandDown()
            && (event.mods.isLeftButtonDown() || event.mods.isRightButtonDown()))
        { marking=true; return; }
        if(onInteractionBegin) onInteractionBegin();
        if(isSquare())
        {
            if(!interactive || !isEnabled() || editor || event.originalComponent!=this || !event.mods.isLeftButtonDown()) return;
            if(event.mods.isCtrlDown() && event.mods.isShiftDown())
            { if(onResetRequested) onResetRequested(); return; }
            pressed=true;
            if(numeric || choiceCount>1) squareDrag.begin(target,choiceCount,dragUnitStep);
            repaint(); return;
        }
        if(canAdjust() && event.mods.isCtrlDown()
            && direct.begin(event.getEventRelativeTo(this),getLocalBounds(),target)) return;
        if(event.originalComponent != this || event.mods.isCtrlDown()) return;
        pressed=interactive && event.mods.isLeftButtonDown();
    }
    void mouseDrag(const juce::MouseEvent& event) override
    {
        if(isSquare())
        {
            squareDrag.drag(event,ana::ui::square::rail(getLocalBounds()).getWidth()-ana::ui::square::indicatorSize);
            if(squareDrag.wasDragged()) valueVisible=true;
            repaint(); return;
        }
        if(!direct.isActive()) return;
        if(event.mods.isCtrlDown()) direct.drag(event); else direct.end();
    }
    void mouseUp(const juce::MouseEvent& event) override
    {
        if(isSquare())
        {
            if(std::exchange(marking,false))
            { if(!event.mouseWasDraggedSinceMouseDown() && getLocalBounds().contains(event.getPosition()) && onMark) onMark(); repaint(); return; }
            const auto dragged=squareDrag.active() && squareDrag.wasDragged();
            squareDrag.end(true);
            const auto clicked=pressed && interactive && isEnabled() && getLocalBounds().contains(event.getPosition())
                && !dragged && !event.mouseWasDraggedSinceMouseDown() && !event.mods.isPopupMenu();
            pressed=false; repaint();
            if(clicked)
            {
                if(choiceCount>1 && onChoiceClicked) onChoiceClicked(event.mods.isShiftDown() ? -1 : 1);
                else if(onClick) onClick();
                else showEditor();
            }
            else if(dragged && choiceCount>1 && onDiscreteEdited)
                onDiscreteEdited();
            return;
        }
        if(direct.isActive()) { direct.end(); return; }
        if(event.originalComponent != this) return;
        const auto clicked=pressed && contains(event.getPosition()) && !event.mouseWasDraggedSinceMouseDown();
        pressed=false;
        if(clicked) { if(onClick) onClick(); else showEditor(); }
    }
    void mouseWheelMove(const juce::MouseEvent& event,const juce::MouseWheelDetails& wheel) override
    {
        if(isSquare())
        {
            // Global marker listeners also receive wheels over sibling controls.
            // Only the actual event target may forward scrolling to the viewport.
            if(event.originalComponent==this) juce::Component::mouseWheelMove(event,wheel);
            return;
        }
        const auto local=event.getEventRelativeTo(this);
        if(canAdjust() && event.mods.isCtrlDown() && (getLocalBounds().contains(local.getPosition())
            || ValueRange::contains(local,getLocalBounds(),target)))
        {
            juce::Slider::ScopedDragNotification gesture(target);
            ValueRange::applyWheel(target,event,wheel,wheelRemainder,lastWheelValue);
            return;
        }
        if(event.originalComponent == this) juce::Component::mouseWheelMove(event,wheel);
    }
    bool isBeingEdited() const { return editor!=nullptr; }
    void showEditor()
    {
        if(!interactive || !isEnabled() || editor || choiceCount>0) return;
        if(auto* text=dynamic_cast<juce::TextEditor*>(juce::Component::getCurrentlyFocusedComponent()))
            if(getTopLevelComponent()->isParentOf(text)) { text->grabKeyboardFocus(); return; }
        hover.inherit(false);
        editor=std::make_unique<ValueEditor>();
        editor->setIndicatorAnchor(this);
        editor->setSquareControl(isSquare());
        if(numeric) editor->setNumericControl(&target);
        // Keep the live editor and its caret in the same coordinates as hit testing.
        if(isSquare())
        {
            editor->getProperties().set("squareValueEditor",true);
            editor->setIndents(0,0);
            editor->setReturnKeyStartsNewLine(false);
        }
        editor->setFont(ana::ui::makeFont());
        editor->setJustification(juce::Justification::centred);
        editor->setColour(juce::TextEditor::textColourId,isSquare()?ana::ui::background:ana::ui::white);
        if(isSquare())
        {
            editor->setColour(juce::TextEditor::highlightedTextColourId,ana::ui::white);
            editor->setColour(juce::CaretComponent::caretColourId,ana::ui::background);
        }
        editor->setColour(juce::TextEditor::backgroundColourId,juce::Colours::transparentBlack);
        editor->setColour(juce::TextEditor::outlineColourId,juce::Colours::transparentBlack);
        editor->setColour(juce::TextEditor::focusedOutlineColourId,juce::Colours::transparentBlack);
        editor->setText(target.getTextFromValue(target.getValue()),false);
        editor->onReturnKey=[this] { hideEditor(false); };
        editor->onEscapeKey=[this] { hideEditor(true); };
        editor->onBackgroundClick=[this] { hideEditor(editor->getText().trim().isEmpty()); };
        addAndMakeVisible(*editor); resized();
        if(onEditorVisibilityChanged) onEditorVisibilityChanged(true);
        editor->enterModalState(true,nullptr,false); editor->selectAll();
        sliderValueChanged(&target);
    }
    void hideEditor(bool discard)
    {
        if(!editor) return;
        auto closing=std::move(editor);
        const auto text=closing->getText();
        closing->exitModalState(0); closing->setVisible(false);
        hover.inherit(interactive && ana::ui::isMouseHovering(*this));
        if(!discard && text != target.getTextFromValue(target.getValue()))
        {
            juce::Slider::ScopedDragNotification gesture(target);
            target.setValue(juce::jlimit(target.getMinimum(),target.getMaximum(),target.getValueFromText(text)),
                            juce::sendNotificationSync);
        }
        if(onEditorVisibilityChanged) onEditorVisibilityChanged(false);
        sliderValueChanged(&target);
    }
    void paintRangeIndicator(juce::Graphics& g)
    {
        if (!isSquare() && numeric && markerVisible())
            ValueRange::paint(g, getLocalBounds(), target, interactive ? ana::ui::white : ana::ui::light);
    }
    static void paintRangeIndicators(juce::Graphics& g,juce::Component& root)
    {
        // SVG paints each marker inside its value group, outside the field's clip.
        if (g.getInternalContext().isVectorDevice()) return;
        const juce::Graphics::ScopedSaveState saved(g);
        const auto exclude=[&](const auto& self,juce::Component& c)->void
        {
            if(bool(c.getProperties()["rangeIndicatorOccluder"]))
            {
                auto bounds=root.getLocalArea(&c,c.getLocalBounds());
                for(auto* parent=c.getParentComponent();parent && parent!=&root;parent=parent->getParentComponent())
                    bounds=bounds.getIntersection(root.getLocalArea(parent,parent->getLocalBounds()));
                g.excludeClipRegion(bounds); return;
            }
            for(auto* child:c.getChildren()) if(child->isVisible()) self(self,*child);
        };
        exclude(exclude,root);
        const auto visit=[&](const auto& self,juce::Component& c)->void
        {
            const juce::Graphics::ScopedSaveState clip(g);
            if(dynamic_cast<juce::Viewport*>(&c))
            {
                const auto bounds=root.getLocalArea(&c,c.getLocalBounds());
                g.reduceClipRegion(bounds.withTop(bounds.getY()-4));
            }
            if(auto* field=dynamic_cast<NumericValueBox*>(&c))
            {
                if(!field->isSquare() && field->numeric && field->markerVisible())
                    ValueRange::paint(g,root.getLocalArea(field,field->getLocalBounds()),field->target,
                        field->interactive?ana::ui::white:ana::ui::light);
                return;
            }
            for(auto* child:c.getChildren()) if(child->isVisible()) self(self,*child);
        };
        visit(visit,root);
    }
    std::function<juce::String()> displayTextProvider;
    std::function<void()> onClick;
    std::function<void()> onDiscreteEdited;
    std::function<void(bool)> onEditorVisibilityChanged;
private:
    void timerCallback() override
    {
        const auto ownInteraction=(squareDrag.active() && squareDrag.wasDragged()) || pressed || editor!=nullptr;
        const auto hovering=isShowing()?ana::ui::square::pointerOver(*this,getLocalBounds()):mouseInside;
        if(ana::ui::square::hoverBlocked()) mouseInside=false;
        const auto reveal=interactive && isEnabled()
            && (ownInteraction || (!ana::ui::square::hoverBlocked() && hovering));
        if(reveal==valueVisible)
        {
            transitionAt=0.0;
            if(!reveal && !isShowing()) stopTimer();
            return;
        }
        const auto now=juce::Time::getMillisecondCounterHiRes();
        if(transitionAt==0.0) transitionAt=now+50.0;
        if(now>=transitionAt) { valueVisible=reveal; transitionAt=0.0; repaint(); }
    }
    SlotIndicator slotIndicator;
    bool bottomSlotVisible=true;
    juce::Rectangle<int> squareCaptionBounds() const
    {
        auto text=ana::ui::square::textBounds(getLocalBounds(),numeric || choiceCount>1);
        if(bottomSlotVisible) text.removeFromBottom(14);
        return text;
    }
    void paintSquare(juce::Graphics& g)
    {
        if(isShowing() && !isTimerRunning()) startTimer(10);
        const auto enabled=interactive && isEnabled();
        auto position=squareDrag.active() && squareDrag.wasDragged()
            ? juce::jmin(1.0,squareDrag.visualPosition()) : ValueRange::rangePosition(target);
        if(pressed && choiceCount==2 && !squareDrag.wasDragged()) position=position<0.5 ? 1.0 : 0.0;
        const auto interacting=pressed || (squareDrag.active() && squareDrag.wasDragged())
            || (editor!=nullptr && editor->isIndicatorDragging());
        if(numeric || choiceCount>1) ana::ui::square::paintRail(g,getLocalBounds(),position,enabled,interacting,
            ana::ui::square::pointerOver(*this,ana::ui::square::marker(getLocalBounds(),position)));
        if(isMarked && isMarked() && (numeric || choiceCount>1))
        {
            const auto track=ana::ui::square::rail(getLocalBounds());
            const auto marker=ana::ui::square::marker(getLocalBounds(),position);
            g.setColour(ana::ui::white); g.fillRect(track.getX(),track.getCentreY()-1,track.getWidth(),2); g.fillRect(marker);
            if(enabled && !interacting && ana::ui::square::pointerOver(*this,marker))
            { g.setColour(ana::ui::square::panelBackground); g.fillRect(marker); g.setColour(ana::ui::white); g.drawRect(marker,1); }
        }
        const auto textArea=squareCaptionBounds();
        const auto showValue=valueVisible && (!ana::ui::square::hoverBlocked() || interacting || editor!=nullptr);
        ana::ui::square::paintTextButton(g,textArea,enabled && showValue,
            enabled && (pressed || editor!=nullptr),enabled);
        if(editor) return;
        const auto value=displayTextProvider?displayTextProvider():target.getTextFromValue(target.getValue());
        const auto binary=choiceCount==2 && (value.equalsIgnoreCase("ON") || value.equalsIgnoreCase("OFF"));
        const auto text=showValue && !binary ? value : idleText();
        if(ana::ui::square::drawCaption(g,text,textArea)) ana::ui::scheduleMarqueeRepaint(*this,marqueePending);
    }
    bool markerVisible() const
    {
        if(!isShowing()) return false;
        for(auto* parent=getParentComponent();parent;parent=parent->getParentComponent())
            if(auto* viewport=dynamic_cast<juce::Viewport*>(parent))
            {
                const auto y=viewport->getLocalArea(this,getLocalBounds()).getY();
                if(y<0 || y>=viewport->getHeight()) return false;
            }
        return true;
    }
    bool canAdjust() const
    {
        return interactive && numeric && !editor && markerVisible()
            && juce::Component::getCurrentlyModalComponent()==nullptr;
    }
    void sliderValueChanged(juce::Slider*) override
    {
        auto* root=getTopLevelComponent();
        root->repaint(root->getLocalArea(this,getLocalBounds().expanded(0,4)));
    }
    class ExternalListener final : public juce::MouseListener
    {
    public:
        explicit ExternalListener(NumericValueBox& field):owner(field) {}
        bool accepts(const juce::MouseEvent& e) const
        { return e.originalComponent && e.originalComponent!=&owner && !owner.isParentOf(e.originalComponent)
            && e.originalComponent->getTopLevelComponent()==owner.getTopLevelComponent(); }
        void mouseDown(const juce::MouseEvent& e) override { if(accepts(e)) owner.mouseDown(e); }
        void mouseDrag(const juce::MouseEvent& e) override { if(accepts(e)) owner.mouseDrag(e); }
        void mouseUp(const juce::MouseEvent& e) override { if(accepts(e)) owner.mouseUp(e); }
        void mouseWheelMove(const juce::MouseEvent& e,const juce::MouseWheelDetails& w) override
        { if(accepts(e)) owner.mouseWheelMove(e,w); }
    private: NumericValueBox& owner;
    } external { *this };
    std::function<juce::String()> idleText;
    ana::ui::square::Drag squareDrag;
    int choiceCount=0;
    double dragUnitStep=0.0;
    bool valueVisible=false,mouseInside=false,marking=false;
    double transitionAt=0.0;
    NumericTextRaster textRaster;
    juce::Slider& target;
    ValueRange direct;
    std::unique_ptr<ValueEditor> editor;
    HoverAnimation hover { *this };
    double wheelRemainder=0.0,lastWheelValue=0.0;
    bool numeric=true,interactive=true,pressed=false,marqueePending=false;
};
