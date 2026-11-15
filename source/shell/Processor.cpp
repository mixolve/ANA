#include "shared/shell/HostSlots.h"
#include "Processor.h"
#include "shared/spec/SpectrumProcessing.h"
#include "shared/corr/Settings.h"
#include "Editor.h"
#include "OscController.h"
#include "PlaybackRenderer.h"
#include "offline/shell/Analyzer.h"

#include <cmath>
#include <cstdlib>
#include <limits>
#include <optional>
#include "shared/spec/Snapshots.h"
#include "shared/animation/State.h"

namespace
{
std::optional<double> stateNumber(const juce::var& value)
{
    const auto text = value.toString().trim();
    if (text.isEmpty()) return std::nullopt;
    const auto* begin = text.toRawUTF8();
    char* end = nullptr;
    const auto number = std::strtod(begin, &end);
    if (end == begin || *end != '\0' || ! std::isfinite(number)) return std::nullopt;
    return number;
}

bool validStateNumber(const juce::var& value, double low, double high, bool integer = false)
{
    const auto number = stateNumber(value);
    return number && *number >= low && *number <= high && (! integer || std::floor(*number) == *number);
}

// Validate each stored setting independently. Older versions of ANA had a
// strict all-or-nothing validator, which rejected an entire host preset whenever
// the parameter count changed after an update.
bool validStateProperty(const juce::Identifier& key, const juce::var& value)
{
    if (const auto valid = validateAnimationProperty(key, value); valid.has_value() && ! *valid)
        return false;
    for (const auto* boolean : { PluginProcessor::offlineAutomaticRefreshStateKey,
                                PluginProcessor::offlineKeepSecondTakeStateKey,
                                PluginProcessor::oscEnabledStateKey,
                                PluginProcessor::scopFullSourceStateKey,
                                "ui.scop.equal-height", "ui.lvls.parts-centered", "ui.square-resize" })
        if (key == juce::Identifier(boolean) && ! validStateNumber(value, 0, 1, true)) return false;
    if (key == juce::Identifier(PluginProcessor::offlineLocationStateKey)
        && ! validStateNumber(value, 0, 1, true)) return false;
    if (key == juce::Identifier(PluginProcessor::offlineTakeNumberStateKey)
        && ! validStateNumber(value, 1, std::numeric_limits<int>::max(), true)) return false;
    if ((key == juce::Identifier(PluginProcessor::editorWidthStateKey)
         || key == juce::Identifier(PluginProcessor::editorHeightStateKey))
        && ! validStateNumber(value, 1, 32768, true)) return false;
    if ((key == juce::Identifier(PluginProcessor::oscInputPortStateKey)
         || key == juce::Identifier(PluginProcessor::oscOutputPortStateKey))
        && ! validStateNumber(value, 1, 65535, true)) return false;
    if (key == juce::Identifier(PluginProcessor::oscOutputHostStateKey)
        && (value.toString().trim().isEmpty() || value.toString().length() > 253)) return false;
    if (key == juce::Identifier(PluginProcessor::offlineSourceStateKey) && ! value.isString()) return false;
    if (key == juce::Identifier(PluginProcessor::scopSingleViewStateKey)
        && ! validStateNumber(value, -1, ana::dsp::LinkwitzRileyCrossover::numBands - 1, true)) return false;
    for (const auto* weight : PluginProcessor::lvlsSectionWeightStateKeys)
        if (key == juce::Identifier(weight)
            && ! validStateNumber(value, std::numeric_limits<float>::denorm_min(),
                                  std::numeric_limits<float>::max())) return false;
    for (const auto* weight : PluginProcessor::scopBandHeightStateKeys)
        if (key == juce::Identifier(weight)
            && ! validStateNumber(value, std::numeric_limits<float>::denorm_min(),
                                  std::numeric_limits<float>::max())) return false;
    if (key == juce::Identifier("ana.spec.snapshots") && ! ana::spec::SnapshotStore::isValidState(value))
        return false;
    return true;
}

juce::ValueTree compatibleProcessorState(const juce::ValueTree& saved,
                                         juce::AudioProcessorValueTreeState& parameters)
{
    if (! saved.isValid() || saved.getType() != parameters.state.getType())
        return {};

    // Start from the current schema: missing parameters retain their current
    // values, removed parameters are ignored, and valid saved parameters survive.
    auto restored = parameters.copyState();
    // A preset without slot mappings must not inherit the current project's assignments.
    for(int slot=0;slot<HostSlots::count;++slot) restored.removeProperty(HostSlots::key(slot),nullptr);
    for (const auto child : saved)
    {
        if (! child.hasType("PARAM") || ! child.hasProperty("id")
            || ! child.hasProperty("value"))
            continue;
        const auto id = child.getProperty("id").toString();
        const auto* parameter = parameters.getParameter(id);
        const auto value = stateNumber(child.getProperty("value"));
        if (parameter == nullptr || ! value || id == PluginProcessor::cleanViewParameterId)
            continue;

        const auto& range = parameter->getNormalisableRange();
        const auto clamped = juce::jlimit(range.start, range.end, static_cast<float>(*value));
        const auto normalized = range.snapToLegalValue(clamped);
        if (! std::isfinite(normalized))
            continue;
        auto target = restored.getChildWithProperty("id", id);
        if (target.isValid())
            target.setProperty("value", normalized, nullptr);
    }

    // Ignore CLEAN VIEW in old host projects and force it OFF on restore.
    if (auto clean = restored.getChildWithProperty("id", PluginProcessor::cleanViewParameterId); clean.isValid())
        clean.setProperty("value", 0.0f, nullptr);
    const auto hasSizePair = saved.hasProperty(PluginProcessor::editorWidthStateKey)
        && saved.hasProperty(PluginProcessor::editorHeightStateKey);
    for (int index = 0; index < saved.getNumProperties(); ++index)
    {
        const auto key = saved.getPropertyName(index);
        if (! hasSizePair && (key == juce::Identifier(PluginProcessor::editorWidthStateKey)
                              || key == juce::Identifier(PluginProcessor::editorHeightStateKey)))
            continue;
        const auto value = saved.getProperty(key);
        if (validStateProperty(key, value))
            restored.setProperty(key, value, nullptr);
    }
    if(!saved.getChildWithProperty("id",PluginProcessor::dockCountParameterId).isValid())
        if(auto dock=restored.getChildWithProperty("id",PluginProcessor::dockCountParameterId);dock.isValid()) dock.setProperty("value",6.0f,nullptr);
    if(!saved.hasProperty("ui.lvls.parts-centered")) restored.setProperty("ui.lvls.parts-centered",true,nullptr);
    return restored;
}
}

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
    animationParameterFirst=parameters.getParameter(animationParameterDefinitions().front().property)->getParameterIndex();
    animationParameterLast=parameters.getParameter(animationParameterDefinitions().back().property)->getParameterIndex();
    scopActionIndices={parameters.getParameter(scopEqualHeightParameterId)->getParameterIndex(),parameters.getParameter(scopOverallFullParameterId)->getParameterIndex(),parameters.getParameter(scopClearParameterId)->getParameterIndex()};
    displayChoiceIndex=parameters.getParameter(displayChoiceParameterId)->getParameterIndex();
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
        syncAnimationPropertyToParameter(parameters,property);
        const char* action=property==juce::Identifier("ui.scop.equal-height")?scopEqualHeightParameterId
            :property==juce::Identifier(scopFullSourceStateKey)?scopOverallFullParameterId:nullptr;
        if(action!=nullptr)
        {
            auto* parameter=parameters.getParameter(action);
            const auto value=bool(parameters.state[property])?1.0f:0.0f;
            if(!juce::approximatelyEqual(parameter->getValue(),value)) parameter->setValueNotifyingHost(value);
        }
        if(hostSlots) hostSlots->propertyChanged(property);
    }
    notifyHostOfStateChange();
}

void PluginProcessor::parameterValueChanged(int index, float value)
{
    if(!restoringState.load(std::memory_order_acquire) && index>=animationParameterFirst && index<=animationParameterLast) pendingAnimationSync.store(true);
    if(!restoringState.load(std::memory_order_acquire) && index==displayChoiceIndex) pendingDisplaySelection.store(juce::roundToInt(value*6.0f));
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
    if(pendingAnimationSync.exchange(false)) syncAnimationParametersToProperties(parameters);
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
    state.setProperty(offlineLocationStateKey, offlineMonitorLocation.load(std::memory_order_acquire) ? 1 : 0, nullptr);
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
            const juce::MessageManagerLock uiLock;
            if (! uiLock.lockWasGained()) return;
            const juce::ScopedLock stateLock(stateSerializationLock);
            const auto savedState = juce::ValueTree::fromXml(*state);
            auto restoredState = compatibleProcessorState(savedState, parameters);
            if (! restoredState.isValid()) return;
            // Loading a saved state must not mark the project as edited. The
            // listener belongs to the APVTS handle, so reconnect it after restore.
            const auto previousRestoring = restoringState.exchange(true, std::memory_order_acq_rel);
            const juce::ScopeGuard restoreGuard { [this, previousRestoring]
            {
                parameters.state.addListener(this);
                restoringState.store(previousRestoring, std::memory_order_release);
            } };
            cancelPendingUpdate(); pendingDisplaySelection.store(-1); pendingOfflineControls.store(0); pendingAnimationSync.store(false); pendingScopActions.store(0);
            parameters.state.removeListener(this);
            parameters.replaceState(restoredState);
            restoreAnimationParameters(parameters,savedState);
            const auto restoreAction=[&](const char* id,const char* property)
            {
                auto* parameter=parameters.getParameter(id);
                const auto value=savedState.getChildWithProperty("id",id).isValid()?parameter->getValue()>=0.5f:bool(savedState.getProperty(property,false));
                parameter->setValueNotifyingHost(value?1.0f:0.0f);
                parameters.state.setProperty(property,value,nullptr);
            };
            restoreAction(scopEqualHeightParameterId,"ui.scop.equal-height");
            restoreAction(scopOverallFullParameterId,scopFullSourceStateKey);
            parameters.getParameter(scopClearParameterId)->setValueNotifyingHost(0.0f);
            if (const auto* mode = getRawParameterValue(corrModeParameterId))
                constrainCorrRangeForMode(juce::roundToInt(mode->load(std::memory_order_relaxed)));

            {
                const juce::ScopedLock scopedLock(offlineSelectionLock);
                selectedOfflineSourceId = parameters.state.getProperty(
                    offlineSourceStateKey, juce::String()).toString();
                selectedOfflineTakeNumber = juce::jlimit(1, 2, static_cast<int>(parameters.state.getProperty(
                    offlineTakeNumberStateKey, 1)));
            }
            const auto monitor = savedState.getChildWithProperty("id", offlineMonitorParameterId).isValid()
                ? parameters.getParameter(offlineMonitorParameterId)->getValue() >= 0.5f
                : static_cast<int>(parameters.state.getProperty(offlineLocationStateKey, 0)) == 1;
            offlineAutomaticRefresh.store(static_cast<bool>(parameters.state.getProperty(
                offlineAutomaticRefreshStateKey, true)), std::memory_order_release);
            const auto keepTake = savedState.getChildWithProperty("id", offlineKeepTakeParameterId).isValid()
                ? parameters.getParameter(offlineKeepTakeParameterId)->getValue() >= 0.5f
                : static_cast<bool>(parameters.state.getProperty(offlineKeepSecondTakeStateKey, false));
            offlineKeepSecondTake.store(keepTake, std::memory_order_release);
            {
                const juce::ScopedLock scopedLock(offlineSelectionLock);
                offlineAnalysisSourcesInitialised = false;
            }
            offlineMonitorLocation.store(monitor, std::memory_order_release);
            monitorHostBridge.setEnabled(monitor);
            // Root properties preserve compatibility with older host states.
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
