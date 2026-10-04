#include "Processor.h"
#include "shared/spec/SpectrumProcessing.h"
#include "shared/corr/Settings.h"
#include "Editor.h"
#include "OscController.h"
#include "PlaybackRenderer.h"
#include "offline/shell/Analyzer.h"

#include <cmath>

PluginProcessor::PluginProcessor()
    : AudioProcessor(BusesProperties()
          .withInput("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "ANA_PARAMETERS", createParameterLayout())
    , reaperHostBridge([this] (const std::vector<ana::offline::SourceChoice>& choices)
      {
          if (! isOfflineMonitorLocation())
              syncOfflineAnalysisSources();
          if (auto* renderer = getPlaybackRenderer<ana::offline::PlaybackRenderer>())
              return renderer->setHostSourceChoices(choices);
          juce::ignoreUnused(choices);
          return false;
      })
    , monitorHostBridge([this] (const auto&)
      {
          if (isOfflineMonitorLocation())
              syncOfflineAnalysisSources();
          return true;
      }, true)
    , hostAnalysisWorker("ANA Host File Analysis",
      [] (const ana::offline::AnalysisRequest& request,
          const ana::offline::Worker::ShouldCancel& shouldCancel,
          const ana::offline::Worker::ProgressCallback& onProgress)
      {
          return ana::offline::analysePlaybackRegions(
              nullptr, {}, request, request.revision | (uint64_t { 1 } << 62),
              shouldCancel, onProgress);
      })
{
    monitorHostBridge.setEnabled(false);
    oscController = std::make_unique<OscController>(*this);
    reaperHostBridge.start();
    monitorHostBridge.start();
}

PluginProcessor::~PluginProcessor() = default;

OscSettings PluginProcessor::getOscSettings() const
{
    OscSettings settings;
    const auto& state = parameters.state;
    settings.enabled = static_cast<bool>(state.getProperty(oscEnabledStateKey, false));
    settings.inputPort = static_cast<int>(state.getProperty(
        oscInputPortStateKey, OscSettings::defaultInputPort));
    settings.outputHost = state.getProperty(oscOutputHostStateKey, "127.0.0.1").toString();
    settings.outputPort = static_cast<int>(state.getProperty(
        oscOutputPortStateKey, OscSettings::defaultOutputPort));
    return settings;
}

bool PluginProcessor::setOscSettings(const OscSettings& settings)
{
    if (oscController == nullptr || ! oscController->applySettings(settings))
        return false;
    parameters.state.setProperty(oscEnabledStateKey, settings.enabled, nullptr);
    parameters.state.setProperty(oscInputPortStateKey, settings.inputPort, nullptr);
    parameters.state.setProperty(oscOutputHostStateKey, settings.outputHost.trim(), nullptr);
    parameters.state.setProperty(oscOutputPortStateKey, settings.outputPort, nullptr);
    updateHostDisplay(juce::AudioProcessor::ChangeDetails().withNonParameterStateChanged(true));
    return true;
}

bool PluginProcessor::isOscInputPortBusy() const
{
    return oscController != nullptr && oscController->isInputPortBusy();
}

bool PluginProcessor::isParameterEnabled(const char* parameterId) const noexcept
{
    const auto* value = getRawParameterValue(parameterId);
    return value == nullptr || value->load(std::memory_order_relaxed) >= 0.5f;
}

juce::VST3ClientExtensions* PluginProcessor::getVST3ClientExtensions()
{
    return this;
}

void PluginProcessor::setIHostApplication(Steinberg::FUnknown* hostApplication)
{
    reaperHostBridge.setHostApplication(hostApplication);
    monitorHostBridge.setHostApplication(hostApplication);
    monitorHostBridge.setEnabled(isOfflineMonitorLocation());
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
    state.setProperty(offlineLocationStateKey, isOfflineMonitorLocation() ? 1 : 0, nullptr);
    state.setProperty(offlineAutomaticRefreshStateKey, isOfflineAutomaticRefresh(), nullptr);
    state.setProperty(offlineKeepSecondTakeStateKey, isOfflineKeepSecondTake(), nullptr);
    {
        const juce::ScopedLock scopedLock(offlineSelectionLock);
        state.setProperty(offlineSourceStateKey, selectedOfflineSourceId, nullptr);
        state.setProperty(offlineTakeNumberStateKey, selectedOfflineTakeNumber, nullptr);
    }
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
            if (const auto* mode = getRawParameterValue(corrModeParameterId))
                constrainCorrRangeForMode(juce::roundToInt(mode->load(std::memory_order_relaxed)));

            {
                const juce::ScopedLock scopedLock(offlineSelectionLock);
                selectedOfflineSourceId = parameters.state.getProperty(
                    offlineSourceStateKey, juce::String()).toString();
                selectedOfflineTakeNumber = juce::jmax(1, static_cast<int>(parameters.state.getProperty(
                    offlineTakeNumberStateKey, 1)));
            }
            const auto monitor = static_cast<int>(parameters.state.getProperty(offlineLocationStateKey, 0)) == 1;
            offlineAutomaticRefresh.store(static_cast<bool>(parameters.state.getProperty(
                offlineAutomaticRefreshStateKey, true)), std::memory_order_release);
            offlineKeepSecondTake.store(static_cast<bool>(parameters.state.getProperty(
                offlineKeepSecondTakeStateKey, false)), std::memory_order_release);
            {
                const juce::ScopedLock scopedLock(offlineSelectionLock);
                offlineAnalysisSourcesInitialised = false;
            }
            offlineMonitorLocation.store(monitor, std::memory_order_release);
            monitorHostBridge.setEnabled(isOfflineMonitorLocation());
            if (oscController != nullptr)
                oscController->applySettingsAsync(getOscSettings());
            syncOfflineAnalysisSources(true);
            refreshOfflineSelection(getSourceChoices());

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
