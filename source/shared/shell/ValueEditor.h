#pragma once

#include "Controls.h"
#include "Theme.h"
#include "ValueRange.h"
#include "PointerInput.h"
#include "SquareControl.h"

class ValueEditor final : public CopyPasteTextEditor, private juce::Slider::Listener
{
public:
    ValueEditor() { juce::Desktop::getInstance().addGlobalMouseListener(&external); }
    ~ValueEditor() override
    {
        juce::Desktop::getInstance().removeGlobalMouseListener(&external);
        if (target != nullptr) target->removeListener(this);
    }

    void setNumericControl(juce::Slider* control,
                           std::function<double(const juce::String&)> parser = {},
                           std::function<juce::String()> formatter = {})
    {
        if (target != nullptr) target->removeListener(this);
        target = control;
        if (target != nullptr) target->addListener(this);
        juce::ignoreUnused(parser);
        formatText = std::move(formatter);
    }

    void setSquareControl(bool enabled) { squareControl=enabled; }
    void setIndicatorAnchor(juce::Component* anchor) { indicatorAnchor=anchor; }
    bool beginIndicatorDrag(const juce::MouseEvent& event)
    {
        const auto bounds=indicatorAnchor!=nullptr
            ? getLocalArea(indicatorAnchor,indicatorAnchor->getLocalBounds()) : getLocalBounds();
        if(target==nullptr) return false;
        if(squareControl)
        {
            if(!event.mods.isLeftButtonDown()
                || !ana::ui::square::marker(bounds,ValueRange::rangePosition(*target)).contains(event.getPosition())) return false;
            squareIndicator.begin(*target,0);
        }
        else if(!indicator.begin(event,bounds,*target)) return false;
        dragTarget=event.originalComponent;
        return true;
    }
    bool isIndicatorDragging() const { return indicator.isActive() || squareIndicator.active(); }

    std::function<void()> onBackgroundClick;
    bool canModalEventBeSentToComponent(const juce::Component* component) override
    {
        if (ana::ui::isEmptyEditorBackground(component)) return true;
        if (isIndicatorDragging() && component==dragTarget.getComponent()) return true;
        if (target==nullptr) return false;
        const auto bounds=indicatorAnchor!=nullptr
            ? getLocalArea(indicatorAnchor,indicatorAnchor->getLocalBounds()) : getLocalBounds();
        return ValueRange::markerBounds(bounds,*target).contains(
            getLocalPoint(nullptr,juce::Desktop::getMousePosition()));
    }
    void inputAttemptWhenModal() override
    {
        auto* clicked=ana::ui::componentAtPointer(*this);
        if (ana::ui::isEmptyEditorBackground(clicked) && onBackgroundClick) { onBackgroundClick(); return; }
        grabKeyboardFocus();
    }
    void mouseDown(const juce::MouseEvent& event) override
    {
        if (beginIndicatorDrag(event)) return;
        if (event.originalComponent != this && !isParentOf(event.originalComponent))
        {
            if (event.mods.isLeftButtonDown() && ana::ui::isEmptyEditorBackground(event.originalComponent) && onBackgroundClick)
                onBackgroundClick();
            return;
        }
        CopyPasteTextEditor::mouseDown(event);
    }
    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (!isIndicatorDragging()) { CopyPasteTextEditor::mouseDrag(event); return; }
        juce::Component::SafePointer<ValueEditor> safeThis(this);
        if(squareIndicator.active())
        {
            const auto bounds=indicatorAnchor!=nullptr
                ? getLocalArea(indicatorAnchor,indicatorAnchor->getLocalBounds()) : getLocalBounds();
            const auto rail=ana::ui::square::rail(bounds);
            squareIndicator.drag(event,rail.getWidth()-rail.getHeight());
        }
        else indicator.drag(event);
        if (safeThis != nullptr && event.mouseWasDraggedSinceMouseDown())
        {
            setText(formatText ? formatText() : target->getTextFromValue(target->getValue()), false);
            selectAll();
        }
    }
    void mouseUp(const juce::MouseEvent& event) override
    {
        if (isIndicatorDragging()) { squareIndicator.end(true); indicator.end(); dragTarget=nullptr; return; }
        CopyPasteTextEditor::mouseUp(event);
    }

    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override
    {
        // Editing text never turns a wheel event into a parameter gesture.
        if(target==nullptr) CopyPasteTextEditor::mouseWheelMove(event,wheel);
    }

private:
    class ExternalListener final : public juce::MouseListener
    {
    public:
        explicit ExternalListener(ValueEditor& editor) : owner(editor) {}
        bool accepts(const juce::MouseEvent& e) const
        {
            return owner.isShowing() && e.originalComponent != nullptr && e.originalComponent != &owner
                && !owner.isParentOf(e.originalComponent)
                && e.originalComponent->getTopLevelComponent() == owner.getTopLevelComponent();
        }
        void mouseDown(const juce::MouseEvent& e) override
        { if (accepts(e)) owner.mouseDown(e.getEventRelativeTo(&owner)); }
        void mouseDrag(const juce::MouseEvent& e) override
        { if (accepts(e) && owner.isIndicatorDragging()) owner.mouseDrag(e.getEventRelativeTo(&owner)); }
        void mouseUp(const juce::MouseEvent& e) override
        { if (accepts(e) && owner.isIndicatorDragging()) owner.mouseUp(e.getEventRelativeTo(&owner)); }
    private:
        ValueEditor& owner;
    } external { *this };
    void sliderValueChanged(juce::Slider*) override
    {
        if (auto* parent = getParentComponent()) parent->repaint(getBounds().expanded(0, 4));
        else repaint();
    }

    juce::Component::SafePointer<juce::Slider> target;
    std::function<juce::String()> formatText;
    juce::Component::SafePointer<juce::Component> indicatorAnchor;
    juce::Component::SafePointer<juce::Component> dragTarget;
    ValueRange indicator;
    ana::ui::square::Drag squareIndicator;
    bool squareControl=false;
};
