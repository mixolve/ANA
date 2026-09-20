#include "Processor.h"
#include "shared/analyzer/DisplaySettings.h"
#include "Editor.h"
#include "PlaybackRenderer.h"

#include <cmath>

PluginProcessor::PluginProcessor()
    : AudioProcessor(BusesProperties()
          .withInput("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "ANA_PARAMETERS", createParameterLayout())
    , reaperHostBridge([this] (const std::vector<ana::ara::SourceChoice>& choices)
      {
          if (auto* renderer = getPlaybackRenderer<ana::ara::PlaybackRenderer>())
              return renderer->setRtSourceChoices(choices);
          juce::ignoreUnused(choices);
          return false;
      })
{
    reaperHostBridge.start();
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
    reaperHostBridge.setHostApplication(hostApplication);
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
    double tailLength = 0.0;

    if (getTailLengthSecondsForARA(tailLength))
        return tailLength;

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
            if (const auto* mode = parameters.getRawParameterValue(corrModeParameterId))
                constrainCorrRangeForMode(
                    juce::jlimit(0, ana::corr::CorrProcessor::modeCount - 1,
                                 juce::roundToInt(mode->load(std::memory_order_relaxed))));
            activeAnalyzerPage.store(
                ana::analyzerPageFromIndex(static_cast<int>(parameters.state.getProperty(
                    analyzerPageStateKey, static_cast<int>(ana::AnalyzerPage::spec)))),
                std::memory_order_release);

            {
                const juce::ScopedLock scopedLock(araSelectionLock);
                selectedAraSourceId = parameters.state.getProperty(
                    araSourceStateKey, juce::String()).toString();
                selectedAraTakeNumber = juce::jmax(0, static_cast<int>(parameters.state.getProperty(
                    araTakeNumberStateKey, 0)));
            }

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
