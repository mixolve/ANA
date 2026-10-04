#pragma once

#include <JuceHeader.h>
#include <memory>

// Rebinds a UI button to the real parameter of the selected submodule.
class SubmoduleButtonAttachment final : private juce::Timer, private juce::MouseListener
{
public:
    SubmoduleButtonAttachment(juce::AudioProcessorValueTreeState& state,
                              const juce::String& id, juce::Button& button);
    ~SubmoduleButtonAttachment() override;
    void refreshBinding();

private:
    void timerCallback() override { refreshBinding(); }
    void mouseDown(const juce::MouseEvent&) override { refreshBinding(); }
    juce::AudioProcessorValueTreeState& state;
    juce::String sourceId;
    juce::String boundId;
    juce::Button& button;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
};
