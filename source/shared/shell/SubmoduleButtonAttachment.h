#pragma once

#include <JuceHeader.h>
#include <memory>

class SubmoduleButtonAttachment final : private juce::Timer, private juce::MouseListener
{
public:
    SubmoduleButtonAttachment(juce::AudioProcessorValueTreeState& state,
                              const juce::String& id, juce::Button& button, bool fixed = false);
    ~SubmoduleButtonAttachment() override;
    void refreshBinding();

private:
    void timerCallback() override { if (button.isShowing()) refreshBinding(); }
    void mouseDown(const juce::MouseEvent&) override { refreshBinding(); }
    juce::AudioProcessorValueTreeState& state;
    juce::String sourceId;
    juce::String boundId;
    juce::Button& button;
    bool fixedBinding = false;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
};
