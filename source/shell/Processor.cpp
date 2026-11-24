#include "shared/shell/HostSlots.h"
#include "Processor.h"
#include "Editor.h"
#include "OscController.h"
#include "PlaybackRenderer.h"
#include "offline/shell/Analyzer.h"

#include <algorithm>


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
      }, false, [this]
      {
          // ARA playback still needs its track/take mapping. Plain realtime
          // input analysis (including Monitoring FX) needs no file catalogue.
          return isOfflineMode()
              || getPlaybackRenderer<ana::offline::PlaybackRenderer>() != nullptr;
      })
    , monitorHostBridge([this] (const auto&)
      {
          if (isOfflineMonitorLocation())
              syncOfflineAnalysisSources();
          return true;
      }, true, [this] { return isOfflineMode() && isOfflineMonitorLocation(); })
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
    offlineControlIndices = { parameters.getParameter(offlineMonitorParameterId)->getParameterIndex(),
        parameters.getParameter(offlineKeepTakeParameterId)->getParameterIndex(),
        parameters.getParameter(offlineRefreshParameterId)->getParameterIndex() };
    scopActionIndices={parameters.getParameter(scopEqualHeightParameterId)->getParameterIndex(),parameters.getParameter(scopOverallFullParameterId)->getParameterIndex(),parameters.getParameter(scopClearParameterId)->getParameterIndex()};
    displayChoiceIndex=parameters.getParameter(displayChoiceParameterId)->getParameterIndex();
    corrModeIndex=parameters.getParameter(corrModeParameterId)->getParameterIndex();
    moduleIndex=parameters.getParameter(moduleParameterId)->getParameterIndex();
    hostSlots=std::make_unique<HostSlots>(parameters,[this] { triggerAsyncUpdate(); });
    parameters.state.addListener(this);
    for (auto* parameter : juce::AudioProcessor::getParameters())
        parameter->addListener(this);
    monitorHostBridge.setEnabled(false);
    oscController = std::make_unique<OscController>(*this);
    reaperHostBridge.start();
    monitorHostBridge.start();
}

PluginProcessor::~PluginProcessor()
{
    hostSlots.reset();
    parameters.state.removeListener(this);
    for (auto* parameter : juce::AudioProcessor::getParameters())
        parameter->removeListener(this);
    cancelPendingUpdate();
}

void PluginProcessor::valueTreePropertyChanged(juce::ValueTree& tree,const juce::Identifier& property)
{
    if(tree!=parameters.state) return;
    if(!restoringState.load())
    {
        if(hostSlots) hostSlots->propertyChanged(property);
    }
    notifyHostOfStateChange();
}

void PluginProcessor::parameterValueChanged(int index, float value)
{
    if (! restoringState.load(std::memory_order_acquire))
    {
        if (index == displayChoiceIndex)
            pendingDisplaySelection.store(juce::roundToInt(value * 6.0f));
        if (index == corrModeIndex)
        {
            // Host automation bypasses setCorrMode(): reset immediately, but
            // defer parameter range adjustment to the message thread.
            corrProcessor.requestClear();
            pendingCorrModeUpdate.store(true, std::memory_order_release);
        }
        if (index == moduleIndex && juce::roundToInt(value * 3.0f) == 0)
            specProcessor.requestRealtimeReset();
    }
    if (!restoringState.load(std::memory_order_acquire))
        for (size_t i=0; i<offlineControlIndices.size(); ++i)
            if (index==offlineControlIndices[i]) pendingOfflineControls.fetch_or(1u << i);
    if(!restoringState.load(std::memory_order_acquire))
        for(size_t i=0;i<scopActionIndices.size();++i)
            if(index==scopActionIndices[i] && (i!=2 || value>=0.5f)) pendingScopActions.fetch_or(1u<<i);
    if(hostSlots && !restoringState.load(std::memory_order_acquire)) hostSlots->valueChanged(index,value);
    // Non-automatable UI parameters also need an explicit host dirty signal.
    notifyHostOfStateChange();
}

void PluginProcessor::parameterGestureChanged(int index,bool active)
{ if(hostSlots && !restoringState.load(std::memory_order_acquire)) hostSlots->gestureChanged(index,active); }

void PluginProcessor::notifyHostOfStateChange()
{
    if (! restoringState.load(std::memory_order_acquire)) triggerAsyncUpdate();
}

void PluginProcessor::handleAsyncUpdate()
{
    if (restoringState.load(std::memory_order_acquire))
        return;
    if(hostSlots) hostSlots->sync();
    if (pendingCorrModeUpdate.exchange(false, std::memory_order_acq_rel))
    {
        if (const auto* mode = parameters.getRawParameterValue(corrModeParameterId))
            constrainCorrRangeForMode(juce::roundToInt(mode->load(std::memory_order_relaxed)));
    }
    const auto scopActions=pendingScopActions.exchange(0);
    if(scopActions&1u) setScopBandsEqualHeight(parameters.getParameter(scopEqualHeightParameterId)->getValue()>=0.5f);
    if(scopActions&2u) setScopFullSourceView(parameters.getParameter(scopOverallFullParameterId)->getValue()>=0.5f);
    if(scopActions&4u)
    {
        if(!isOfflineMode()) scopClearRevision.fetch_add(1,std::memory_order_release);
        parameters.getParameter(scopClearParameterId)->setValueNotifyingHost(0.0f);
    }
    const auto offlineChanges = pendingOfflineControls.exchange(0);
    if (offlineChanges & 1u) setOfflineMonitorLocation(parameters.getParameter(offlineMonitorParameterId)->getValue() >= 0.5f);
    if (offlineChanges & 2u) setOfflineKeepSecondTake(parameters.getParameter(offlineKeepTakeParameterId)->getValue() >= 0.5f);
    if ((offlineChanges & 4u) && parameters.getParameter(offlineRefreshParameterId)->getValue() >= 0.5f)
    {
        if (isOfflineMode()) forceOfflineRefresh();
        parameters.getParameter(offlineRefreshParameterId)->setValueNotifyingHost(0.0f);
    }
    const auto display=pendingDisplaySelection.exchange(-1);
    if(display>=0)
    {
        const auto set=[this](const char* id,int plain) { if(auto* p=parameters.getParameter(id)) p->setValueNotifyingHost(p->convertTo0to1(float(plain))); };
        if(display<2) { set(specViewParameterId,display); set(moduleParameterId,0); }
        else if(display<5) { set(corrModeParameterId,display==2?2:display==3?0:1); set(moduleParameterId,1); }
        else set(moduleParameterId,display==5?2:3);
    }
    auto details = juce::AudioProcessorListener::ChangeDetails().withNonParameterStateChanged(true);
#if JucePlugin_Build_AU
    details = details.withProgramChanged(true);
#endif
    updateHostDisplay(details);
}

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
    const juce::ScopedLock stateLock(stateSerializationLock);
    const juce::ScopedLock slotLock(hostSlots->snapshotLock());
    hostSlots->sync();
    auto state = parameters.copyState();
    // CLEAN VIEW is session-only: never save a state that can hide the controls on reopen.
    if (auto clean = state.getChildWithProperty("id", cleanViewParameterId); clean.isValid())
        clean.setProperty("value", 0.0f, nullptr);
    state.setProperty(offlineAutomaticRefreshStateKey,
                      offlineAutomaticRefresh.load(std::memory_order_acquire), nullptr);
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
            const juce::MessageManagerLock uiLock;
            if (! uiLock.lockWasGained()) return;
            const juce::ScopedLock stateLock(stateSerializationLock);
            const auto savedState = juce::ValueTree::fromXml(*state);
            if (! savedState.isValid()) return;
            // Loading a saved state must not mark the project as edited. The
            // listener belongs to the APVTS handle, so reconnect it after restore.
            const auto previousRestoring = restoringState.exchange(true, std::memory_order_acq_rel);
            const juce::ScopeGuard restoreGuard { [this, previousRestoring]
            {
                parameters.state.addListener(this);
                restoringState.store(previousRestoring, std::memory_order_release);
            } };
            cancelPendingUpdate(); pendingDisplaySelection.store(-1); pendingOfflineControls.store(0); pendingScopActions.store(0); pendingCorrModeUpdate.store(false);
            parameters.state.removeListener(this);
            parameters.replaceState(savedState);
            parameters.getParameter(scopClearParameterId)->setValueNotifyingHost(0.0f);
            parameters.getParameter(cleanViewParameterId)->setValueNotifyingHost(0.0f);
            if (const auto* mode = getRawParameterValue(corrModeParameterId))
                constrainCorrRangeForMode(juce::roundToInt(mode->load(std::memory_order_relaxed)));

            {
                const juce::ScopedLock scopedLock(offlineSelectionLock);
                selectedOfflineSourceId = parameters.state.getProperty(
                    offlineSourceStateKey, juce::String()).toString();
                selectedOfflineTakeNumber = juce::jlimit(1, 2, static_cast<int>(parameters.state.getProperty(
                    offlineTakeNumberStateKey, 1)));
            }
            const auto monitor = parameters.getParameter(offlineMonitorParameterId)->getValue() >= 0.5f;
            offlineAutomaticRefresh.store(static_cast<bool>(parameters.state.getProperty(
                offlineAutomaticRefreshStateKey, true)), std::memory_order_release);
            const auto keepTake = parameters.getParameter(offlineKeepTakeParameterId)->getValue() >= 0.5f;
            offlineKeepSecondTake.store(keepTake, std::memory_order_release);
            {
                const juce::ScopedLock scopedLock(offlineSelectionLock);
                offlineAnalysisSourcesInitialised = false;
            }
            offlineMonitorLocation.store(monitor, std::memory_order_release);
            monitorHostBridge.setEnabled(monitor);
            parameters.getParameter(offlineMonitorParameterId)->setValueNotifyingHost(monitor ? 1.0f : 0.0f);
            parameters.getParameter(offlineKeepTakeParameterId)->setValueNotifyingHost(isOfflineKeepSecondTake() ? 1.0f : 0.0f);
            parameters.getParameter(offlineRefreshParameterId)->setValueNotifyingHost(0.0f);
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
            if(hostSlots) hostSlots->refresh();
            stateRevision.fetch_add(1, std::memory_order_release);
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PluginProcessor();
}
