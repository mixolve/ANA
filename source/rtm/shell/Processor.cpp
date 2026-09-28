#include "Processor.h"
#include "shared/analyzer/DisplaySettings.h"
#include "shared/corr/Settings.h"
#include "Editor.h"

#include <cmath>

namespace
{
int currentCorrMode(const PluginProcessor& processor) noexcept
{
    if (const auto* value = processor.getParameters().getRawParameterValue(
            PluginProcessor::corrModeParameterId))
        return juce::jlimit(0, ana::corr::CorrProcessor::modeCount - 1,
                            juce::roundToInt(value->load(std::memory_order_relaxed)));
    return 0;
}

void snapshotCurrentCorrMode(const PluginProcessor& processor, juce::ValueTree& state)
{
    const auto mode = currentCorrMode(processor);
    state.setProperty(ana::corr::modeStateKey, mode, nullptr);
    for (const auto* parameterId : ana::corr::modeSettingParameterIds)
        if (const auto* parameter = processor.getParameters().getParameter(parameterId))
            state.setProperty(ana::corr::modeSettingStateKey(mode, parameterId),
                              parameter->getValue(), nullptr);
}

int restoreCurrentCorrMode(PluginProcessor& processor)
{
    auto mode = currentCorrMode(processor);
    if (processor.getParameters().state.hasProperty(ana::corr::modeStateKey))
        mode = juce::jlimit(0, ana::corr::CorrProcessor::modeCount - 1,
                            static_cast<int>(processor.getParameters().state.getProperty(
                                ana::corr::modeStateKey)));

    if (auto* modeParameter = processor.getParameters().getParameter(
            PluginProcessor::corrModeParameterId))
        modeParameter->setValue(modeParameter->convertTo0to1(static_cast<float>(mode)));

    for (const auto* parameterId : ana::corr::modeSettingParameterIds)
    {
        const auto key = ana::corr::modeSettingStateKey(mode, parameterId);
        auto* parameter = processor.getParameters().getParameter(parameterId);
        if (parameter != nullptr && processor.getParameters().state.hasProperty(key))
            parameter->setValue(juce::jlimit(
                0.0f, 1.0f,
                static_cast<float>(processor.getParameters().state.getProperty(key))));
    }
    return mode;
}
}

PluginProcessor::PluginProcessor()
    : AudioProcessor(BusesProperties()
          .withInput("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "ANA_PARAMETERS", createParameterLayout())
{
}

PluginProcessor::~PluginProcessor() = default;

bool PluginProcessor::isParameterEnabled(const char* parameterId) const noexcept
{
    const auto* value = parameters.getRawParameterValue(parameterId);
    return value == nullptr || value->load(std::memory_order_relaxed) >= 0.5f;
}

juce::VST3ClientExtensions* PluginProcessor::getVST3ClientExtensions()
{
    return this;
}

void PluginProcessor::setIHostApplication(Steinberg::FUnknown* hostApplication)
{
    juce::ignoreUnused(hostApplication);
}

bool PluginProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto input = layouts.getMainInputChannelSet();
    const auto output = layouts.getMainOutputChannelSet();

    return input == output
        && (output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo());
}

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new PluginEditor(*this);
}

bool PluginProcessor::hasEditor() const
{
    return true;
}

const juce::String PluginProcessor::getName() const
{
    return JucePlugin_Name;
}

bool PluginProcessor::acceptsMidi() const
{
    return false;
}

bool PluginProcessor::producesMidi() const
{
    return false;
}

bool PluginProcessor::isMidiEffect() const
{
    return false;
}

double PluginProcessor::getTailLengthSeconds() const
{

    return 0.0;
}

int PluginProcessor::getNumPrograms()
{
    return 1;
}

int PluginProcessor::getCurrentProgram()
{
    return 0;
}

void PluginProcessor::setCurrentProgram(int)
{
}

const juce::String PluginProcessor::getProgramName(int)
{
    return {};
}

void PluginProcessor::changeProgramName(int, const juce::String&)
{
}

void PluginProcessor::getStateInformation(juce::MemoryBlock& destination)
{
    auto state = parameters.copyState();
    snapshotCurrentCorrMode(*this, state);
    const auto editorWidth = lastEditorWidth.load(std::memory_order_relaxed);
    const auto editorHeight = lastEditorHeight.load(std::memory_order_relaxed);

    if (editorWidth > 0 && editorHeight > 0)
    {
        state.setProperty(editorWidthStateKey, editorWidth, nullptr);
        state.setProperty(editorHeightStateKey, editorHeight, nullptr);
    }

    if (auto stateXml = state.createXml())
        copyXmlToBinary(*stateXml, destination);
}

void PluginProcessor::setStateInformation(const void* data, const int sizeInBytes)
{
    if (auto state = getXmlFromBinary(data, sizeInBytes))
        if (state->hasTagName(parameters.state.getType()))
        {
            auto restoredState = juce::ValueTree::fromXml(*state);
            parameters.replaceState(restoredState);
            constrainCorrRangeForMode(restoreCurrentCorrMode(*this));
            activeAnalyzerPage.store(
                ana::analyzerPageFromIndex(static_cast<int>(parameters.state.getProperty(
                    analyzerPageStateKey, static_cast<int>(ana::AnalyzerPage::spec)))),
                std::memory_order_release);


            lastEditorWidth.store(
                std::max(0, static_cast<int>(parameters.state.getProperty(editorWidthStateKey, 0))),
                std::memory_order_relaxed);
            lastEditorHeight.store(
                std::max(0, static_cast<int>(parameters.state.getProperty(editorHeightStateKey, 0))),
                std::memory_order_relaxed);
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PluginProcessor();
}
