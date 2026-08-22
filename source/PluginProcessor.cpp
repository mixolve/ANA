#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <cmath>

namespace
{
juce::String formatFrequency(const float value)
{
    if (value >= 1000.0f)
        return juce::String(value / 1000.0f, value >= 10000.0f ? 1 : 2) + " KHZ";

    return juce::String(value, value >= 100.0f ? 0 : 1) + " HZ";
}

juce::String formatScopeTime(const float milliseconds)
{
    const auto seconds = milliseconds / 1000.0f;
    return juce::String(seconds, seconds < 10.0f ? 1 : 0) + " S";
}
} // namespace

AnaAudioProcessor::AnaAudioProcessor()
    : AudioProcessor(BusesProperties()
          .withInput("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "ANA_PARAMETERS", createParameterLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout AnaAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto timeRange = juce::NormalisableRange<float> { 1000.0f, 30000.0f, 100.0f };
    timeRange.setSkewForCentre(5000.0f);
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { scopeTimeParameterId, 1 },
        "SCOPE / TIME",
        timeRange,
        1000.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("MS")
            .withStringFromValueFunction([] (const float value, int)
            {
                return formatScopeTime(value);
            })));

    const juce::StringArray scopeChannelModes { "LEFT", "RIGHT", "MID", "SIDE" };

    for (size_t bandIndex = 0; bandIndex < scopeChannelModeParameterIds.size(); ++bandIndex)
    {
        layout.add(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID { scopeChannelModeParameterIds[bandIndex], 1 },
            "SCOPE / BAND " + juce::String(static_cast<int>(bandIndex + 1)) + " / CHANNEL",
            scopeChannelModes,
            static_cast<int>(ana::ScopeChannelMode::mid),
            juce::AudioParameterChoiceAttributes().withAutomatable(false).withMeta(true)));
    }

    layout.add(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID { activeSplitCountParameterId, 1 },
        "CROSSOVER / COUNT",
        0,
        static_cast<int>(ana::dsp::Crossover::numSplits),
        static_cast<int>(ana::dsp::Crossover::numSplits),
        juce::AudioParameterIntAttributes().withAutomatable(false).withMeta(true)));

    constexpr ana::dsp::Crossover::SplitFrequencies defaults { 134.0, 523.0, 2093.0, 5000.0, 10000.0 };

    for (size_t index = 0; index < defaults.size(); ++index)
    {
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { crossoverParameterIds[index], 1 },
            "CROSSOVER / " + juce::String(static_cast<int>(index + 1)),
            juce::NormalisableRange<float> { 20.0f, 20000.0f, 0.01f },
            static_cast<float>(defaults[index]),
            juce::AudioParameterFloatAttributes()
                .withAutomatable(false)
                .withMeta(true)
                .withStringFromValueFunction([] (const float value, int)
                {
                    return formatFrequency(value);
                })));
    }

    return layout;
}

void AnaAudioProcessor::prepareToPlay(const double sampleRate, int)
{
    multibandScope.prepare(sampleRate);
}

void AnaAudioProcessor::releaseResources()
{
    multibandScope.reset();
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

    multibandScope.setCrossoverSettings(getActiveSplitCount(), getCrossoverFrequencies());
    multibandScope.processBlock(buffer);

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

void AnaAudioProcessor::getStateInformation(juce::MemoryBlock& destination)
{
    if (auto state = parameters.copyState().createXml())
        copyXmlToBinary(*state, destination);
}

void AnaAudioProcessor::setStateInformation(const void* data, const int sizeInBytes)
{
    if (auto state = getXmlFromBinary(data, sizeInBytes))
        if (state->hasTagName(parameters.state.getType()))
            parameters.replaceState(juce::ValueTree::fromXml(*state));
}

double AnaAudioProcessor::getScopeTimeMilliseconds() const noexcept
{
    if (const auto* value = parameters.getRawParameterValue(scopeTimeParameterId))
        return static_cast<double>(value->load(std::memory_order_relaxed));

    return 1000.0;
}

size_t AnaAudioProcessor::getActiveSplitCount() const noexcept
{
    if (const auto* value = parameters.getRawParameterValue(activeSplitCountParameterId))
    {
        const auto count = static_cast<int>(std::round(value->load(std::memory_order_relaxed)));
        return static_cast<size_t>(juce::jlimit(0, static_cast<int>(ana::dsp::Crossover::numSplits), count));
    }

    return ana::dsp::Crossover::numSplits;
}

ana::ScopeChannelMode AnaAudioProcessor::getScopeChannelMode(const size_t bandIndex) const noexcept
{
    if (bandIndex < scopeChannelModeParameterIds.size())
    {
        if (const auto* value = parameters.getRawParameterValue(scopeChannelModeParameterIds[bandIndex]))
        {
            const auto mode = juce::jlimit(0, 3, static_cast<int>(std::round(value->load(std::memory_order_relaxed))));
            return static_cast<ana::ScopeChannelMode>(mode);
        }
    }

    return ana::ScopeChannelMode::mid;
}

ana::dsp::Crossover::SplitFrequencies AnaAudioProcessor::getCrossoverFrequencies() const noexcept
{
    ana::dsp::Crossover::SplitFrequencies frequencies {};

    for (size_t index = 0; index < frequencies.size(); ++index)
    {
        if (const auto* value = parameters.getRawParameterValue(crossoverParameterIds[index]))
            frequencies[index] = static_cast<double>(value->load(std::memory_order_relaxed));
    }

    return frequencies;
}

void AnaAudioProcessor::setActiveSplitCount(const size_t splitCount)
{
    auto* parameter = parameters.getParameter(activeSplitCountParameterId);
    if (parameter == nullptr)
        return;

    const auto plainValue = static_cast<float>(std::min(splitCount, ana::dsp::Crossover::numSplits));
    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost(parameter->convertTo0to1(plainValue));
    parameter->endChangeGesture();
}

void AnaAudioProcessor::setScopeChannelMode(const size_t bandIndex, const ana::ScopeChannelMode mode)
{
    if (bandIndex >= scopeChannelModeParameterIds.size())
        return;

    auto* parameter = parameters.getParameter(scopeChannelModeParameterIds[bandIndex]);
    if (parameter == nullptr)
        return;

    const auto plainValue = static_cast<float>(mode);
    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost(parameter->convertTo0to1(plainValue));
    parameter->endChangeGesture();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AnaAudioProcessor();
}
