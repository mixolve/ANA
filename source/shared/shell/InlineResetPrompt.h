#pragma once
#include "Controls.h"
class InlineResetPrompt final : public juce::Component
{
public:
    InlineResetPrompt(juce::Rectangle<int> anchorIn, bool selected,
                      std::function<void()> applyAction, std::function<void()> closeAction)
        : anchor(anchorIn), onClose(std::move(closeAction))
    {
        setInterceptsMouseClicks(false, true);
        button.setToggleState(selected, juce::dontSendNotification);
        button.onClick = [this, apply = std::move(applyAction)]
        {
            auto close = onClose;
            if (close) juce::MessageManager::callAsync([safe = juce::Component::SafePointer<InlineResetPrompt>(this), callback = std::move(close)] { if (safe != nullptr) callback(); });
            if (apply) apply();
        };
        addAndMakeVisible(button);
        juce::Desktop::getInstance().addGlobalMouseListener(this);
    }
    ~InlineResetPrompt() override { juce::Desktop::getInstance().removeGlobalMouseListener(this); }
    void resized() override { button.setBounds(anchor); }
private:
    void mouseDown(const juce::MouseEvent& event) override
    {
        if (event.eventComponent == &button) return;
        auto close = onClose;
        if (close) juce::MessageManager::callAsync([safe = juce::Component::SafePointer<InlineResetPrompt>(this), callback = std::move(close)] { if (safe != nullptr) callback(); });
    }
    juce::Rectangle<int> anchor;
    ControlButton button { "refr" };
    std::function<void()> onClose;
};
