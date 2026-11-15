#pragma once

#include "shared/shell/Controls.h"
#include "shared/shell/NumericValueBox.h"

#include <functional>

class PluginProcessor;

class OscPanel final : public juce::Component, private juce::Timer
{
public:
    explicit OscPanel(PluginProcessor&);
    ~OscPanel() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void refresh();
    void dismissEditor();

    std::function<void(bool)> onTextEditingChanged;

private:
    void beginEdit(ControlButton&, const juce::String&,
                   std::function<bool(const juce::String&)>);
    void finishEdit(bool shouldCommit);
    void timerCallback() override;

    PluginProcessor& processor;
    juce::Slider enableSlider;
    NumericValueBox enableControl { enableSlider };
    ControlButton statusControl { "FREE" };
    ControlButton inputPortButton { "9000" };
    ControlButton outputPortButton { "9001" };
    ControlButton outputHostButton { "127.0.0.1" };
    ExplicitCommitTextEditor valueEditor;
    ControlButton* editingButton = nullptr;
    std::function<bool(const juce::String&)> applyEditedValue;
};
