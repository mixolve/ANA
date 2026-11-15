#pragma once

#include "Controls.h"
#include "WindowDragHandle.h"

class AuxiliaryWindowFooter final : public juce::Component
{
public:
    AuxiliaryWindowFooter() : moveHandle(*this)
    {
        setName("window-actions");
        // Window actions finish pending inline edits before running.
        getProperties().set("emptyEditorBackground",true);
        moveHandle.getProperties().set("emptyEditorBackground",true);
        closeButton.getProperties().set("emptyEditorBackground",true);
        moveHandle.setName("move-window");
        closeButton.setName("close-window");
        closeButton.setTooltip("CLOSE WINDOW");
        closeButton.onClick=[this]
        {
            if (auto* window=findParentComponentOfClass<juce::DocumentWindow>())
                window->closeButtonPressed();
        };
        addAndMakeVisible(moveHandle);
        addAndMakeVisible(closeButton);
    }
    void placeIn(juce::Rectangle<int> row)
    {
        setBounds(row.removeFromRight(ana::ui::dragHandleSlotWidth+closeButton.getPreferredWidth()+ana::ui::gap.pixels()));
    }
    void resized() override
    {
        auto row=getLocalBounds();
        closeButton.setBounds(row.removeFromRight(closeButton.getPreferredWidth()));
        ana::ui::gap.removeFromRight(row);
        moveHandle.setBounds(row);
    }
private:
    WindowDragHandle moveHandle;
    ControlButton closeButton { "X" };
};
