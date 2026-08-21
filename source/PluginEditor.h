#pragma once

#include <JuceHeader.h>

class AnaAudioProcessor;

class AnaAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit AnaAudioProcessorEditor(AnaAudioProcessor& processorRef);
    ~AnaAudioProcessorEditor() override = default;

    void paint(juce::Graphics& graphics) override;
    void resized() override;

private:
    AnaAudioProcessor& audioProcessor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnaAudioProcessorEditor)
};
