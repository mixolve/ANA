#include "PluginEditor.h"
#include "PluginProcessor.h"

AnaAudioProcessorEditor::AnaAudioProcessorEditor(AnaAudioProcessor& processorRef)
    : AudioProcessorEditor(&processorRef), audioProcessor(processorRef)
{
    setSize(420, 260);
}

void AnaAudioProcessorEditor::paint(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colour(0xff121417));
    graphics.setColour(juce::Colour(0xffdce8ee));
    graphics.setFont(juce::FontOptions(34.0f, juce::Font::bold));
    graphics.drawFittedText("ANA", getLocalBounds(), juce::Justification::centred, 1);
}

void AnaAudioProcessorEditor::resized()
{
    juce::ignoreUnused(audioProcessor);
}
