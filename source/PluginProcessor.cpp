#include "PluginProcessor.h"
#include "PluginEditor.h"

AnaAudioProcessor::AnaAudioProcessor()
    : AudioProcessor(BusesProperties()
          .withInput("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
}

void AnaAudioProcessor::prepareToPlay(double, int)
{
}

void AnaAudioProcessor::releaseResources()
{
}

bool AnaAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto input = layouts.getMainInputChannelSet();
    const auto output = layouts.getMainOutputChannelSet();

    return ! input.isDisabled() && input == output;
}

void AnaAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused(noDenormals);

    for (auto channel = getTotalNumInputChannels(); channel < getTotalNumOutputChannels(); ++channel)
        buffer.clear(channel, 0, buffer.getNumSamples());
}

juce::AudioProcessorEditor* AnaAudioProcessor::createEditor()
{
    return new AnaAudioProcessorEditor(*this);
}

bool AnaAudioProcessor::hasEditor() const
{
    return true;
}

const juce::String AnaAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool AnaAudioProcessor::acceptsMidi() const
{
    return false;
}

bool AnaAudioProcessor::producesMidi() const
{
    return false;
}

bool AnaAudioProcessor::isMidiEffect() const
{
    return false;
}

double AnaAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int AnaAudioProcessor::getNumPrograms()
{
    return 1;
}

int AnaAudioProcessor::getCurrentProgram()
{
    return 0;
}

void AnaAudioProcessor::setCurrentProgram(int)
{
}

const juce::String AnaAudioProcessor::getProgramName(int)
{
    return {};
}

void AnaAudioProcessor::changeProgramName(int, const juce::String&)
{
}

void AnaAudioProcessor::getStateInformation(juce::MemoryBlock&)
{
}

void AnaAudioProcessor::setStateInformation(const void*, int)
{
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AnaAudioProcessor();
}
