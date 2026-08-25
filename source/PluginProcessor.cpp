#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ara/AraEditorRenderer.h"
#include "ara/AraPlaybackRenderer.h"

#include <algorithm>
#include <cmath>

#if JucePlugin_Build_VST3
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wshadow-field-in-constructor"
#include <pluginterfaces/base/funknown.h>
#pragma clang diagnostic pop
#endif

namespace
{
#if JucePlugin_Build_VST3
struct MediaTrack;
struct MediaItem;
struct MediaItem_Take;
struct PCM_source;

class IReaperHostApplication : public Steinberg::FUnknown
{
public:
    virtual void* PLUGIN_API getReaperApi(const char* functionName) = 0;
    virtual void* PLUGIN_API getReaperParent(Steinberg::uint32 which) = 0;
    virtual void* PLUGIN_API reaperExtended(Steinberg::uint32 call,
                                             void* parameter1,
                                             void* parameter2,
                                             void* parameter3) = 0;
};

static constexpr Steinberg::TUID reaperHostApplicationIid = INLINE_UID(
    0x79655E36, 0x77EE4267, 0xA573FEF7, 0x4912C27C);

template <typename Function>
Function getReaperApi(IReaperHostApplication& host, const char* name)
{
    return reinterpret_cast<Function>(host.getReaperApi(name));
}

juce::String getReaperObjectString(const auto function,
                                   auto* object,
                                   const char* attribute)
{
    std::array<char, 4096> buffer {};
    if (function == nullptr || object == nullptr
        || ! function(object, attribute, buffer.data(), false))
        return {};

    return juce::String::fromUTF8(buffer.data());
}

juce::String getReaperSourceName(const auto getSourceFileName, PCM_source* source)
{
    std::array<char, 4096> buffer {};
    if (getSourceFileName == nullptr || source == nullptr)
        return {};

    getSourceFileName(source, buffer.data(), static_cast<int>(buffer.size()));
    const auto path = juce::String::fromUTF8(buffer.data());
    return path.isNotEmpty() ? juce::File(path).getFileName() : juce::String();
}

juce::String getRuntimeObjectId(const char* prefix, const void* object)
{
    return juce::String(prefix) + juce::String::toHexString(
        static_cast<juce::int64>(reinterpret_cast<intptr_t>(object)));
}
#endif

juce::String formatFrequency(const float value)
{
    return juce::String(value, value >= 100.0f ? 0 : 1);
}

juce::String formatScopeTime(const float milliseconds)
{
    const auto seconds = milliseconds / 1000.0f;
    return juce::String(seconds, seconds < 10.0f ? 1 : 0);
}
} // namespace

AnaAudioProcessor::AnaAudioProcessor()
    : AudioProcessor(BusesProperties()
          .withInput("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "ANA_PARAMETERS", createParameterLayout())
{
}

AnaAudioProcessor::~AnaAudioProcessor()
{
    setIHostApplication(nullptr);
}

juce::VST3ClientExtensions* AnaAudioProcessor::getVST3ClientExtensions()
{
    return this;
}

void AnaAudioProcessor::setIHostApplication(Steinberg::FUnknown* hostApplication)
{
    const juce::ScopedLock scopedLock(reaperHostLock);

#if JucePlugin_Build_VST3
    if (reaperHostApplication != nullptr)
    {
        static_cast<IReaperHostApplication*>(reaperHostApplication)->release();
        reaperHostApplication = nullptr;
    }

    if (hostApplication != nullptr)
    {
        void* queriedInterface = nullptr;
        if (hostApplication->queryInterface(reaperHostApplicationIid, &queriedInterface)
                == Steinberg::kResultOk
            && queriedInterface != nullptr)
            reaperHostApplication = queriedInterface;
    }
#else
    juce::ignoreUnused(hostApplication);
#endif
}

std::vector<ana::OfflineSourceTakeChoice> AnaAudioProcessor::getReaperSourceTakeChoices() const
{
    std::vector<ana::OfflineSourceTakeChoice> choices;

#if JucePlugin_Build_VST3
    const juce::ScopedLock scopedLock(reaperHostLock);
    auto* host = static_cast<IReaperHostApplication*>(reaperHostApplication);
    if (host == nullptr)
        return choices;

    using CountTrackMediaItems = int (*)(MediaTrack*);
    using GetTrackMediaItem = MediaItem* (*)(MediaTrack*, int);
    using GetMediaItemNumTakes = int (*)(MediaItem*);
    using GetMediaItemTake = MediaItem_Take* (*)(MediaItem*, int);
    using GetActiveTake = MediaItem_Take* (*)(MediaItem*);
    using GetMediaItemTakeSource = PCM_source* (*)(MediaItem_Take*);
    using GetMediaSourceFileName = void (*)(PCM_source*, char*, int);
    using GetMediaItemInfoValue = double (*)(MediaItem*, const char*);
    using GetMediaItemTakeInfoValue = double (*)(MediaItem_Take*, const char*);
    using GetSetMediaItemInfoString = bool (*)(MediaItem*, const char*, char*, bool);
    using GetSetMediaItemTakeInfoString = bool (*)(MediaItem_Take*, const char*, char*, bool);

    const auto countTrackMediaItems = getReaperApi<CountTrackMediaItems>(
        *host, "CountTrackMediaItems");
    const auto getTrackMediaItem = getReaperApi<GetTrackMediaItem>(*host, "GetTrackMediaItem");
    const auto getMediaItemNumTakes = getReaperApi<GetMediaItemNumTakes>(
        *host, "GetMediaItemNumTakes");
    const auto getMediaItemTake = getReaperApi<GetMediaItemTake>(*host, "GetMediaItemTake");
    const auto getActiveTake = getReaperApi<GetActiveTake>(*host, "GetActiveTake");
    const auto getMediaItemTakeSource = getReaperApi<GetMediaItemTakeSource>(
        *host, "GetMediaItemTake_Source");
    const auto getMediaSourceFileName = getReaperApi<GetMediaSourceFileName>(
        *host, "GetMediaSourceFileName");
    const auto getMediaItemInfoValue = getReaperApi<GetMediaItemInfoValue>(
        *host, "GetMediaItemInfo_Value");
    const auto getMediaItemTakeInfoValue = getReaperApi<GetMediaItemTakeInfoValue>(
        *host, "GetMediaItemTakeInfo_Value");
    const auto getSetMediaItemInfoString = getReaperApi<GetSetMediaItemInfoString>(
        *host, "GetSetMediaItemInfo_String");
    const auto getSetMediaItemTakeInfoString = getReaperApi<GetSetMediaItemTakeInfoString>(
        *host, "GetSetMediaItemTakeInfo_String");

    auto* track = static_cast<MediaTrack*>(host->getReaperParent(1));
    if (track == nullptr || countTrackMediaItems == nullptr || getTrackMediaItem == nullptr
        || getMediaItemNumTakes == nullptr || getMediaItemTake == nullptr
        || getMediaItemTakeSource == nullptr || getMediaSourceFileName == nullptr
        || getMediaItemInfoValue == nullptr || getMediaItemTakeInfoValue == nullptr)
        return choices;

    const auto itemCount = std::max(0, countTrackMediaItems(track));
    for (int itemIndex = 0; itemIndex < itemCount; ++itemIndex)
    {
        auto* item = getTrackMediaItem(track, itemIndex);
        if (item == nullptr)
            continue;

        auto itemId = getReaperObjectString(getSetMediaItemInfoString, item, "GUID");
        if (itemId.isEmpty())
            itemId = getRuntimeObjectId("ITEM-", item);
        itemId = "REAPER:" + itemId;

        const auto itemStart = getMediaItemInfoValue(item, "D_POSITION");
        const auto itemDuration = std::max(0.0, getMediaItemInfoValue(item, "D_LENGTH"));
        auto* activeTake = getActiveTake != nullptr ? getActiveTake(item) : nullptr;
        auto activeSourceName = getReaperSourceName(
            getMediaSourceFileName,
            activeTake != nullptr ? getMediaItemTakeSource(activeTake) : nullptr);

        const auto takeCount = std::max(0, getMediaItemNumTakes(item));
        for (int takeIndex = 0; takeIndex < takeCount; ++takeIndex)
        {
            auto* take = getMediaItemTake(item, takeIndex);
            if (take == nullptr)
                continue;

            const auto audioSourceName = getReaperSourceName(
                getMediaSourceFileName, getMediaItemTakeSource(take));
            if (activeSourceName.isEmpty())
                activeSourceName = audioSourceName;

            auto takePersistentId = getReaperObjectString(
                getSetMediaItemTakeInfoString, take, "GUID");
            auto takeId = takePersistentId;
            if (takeId.isEmpty())
                takeId = getRuntimeObjectId("TAKE-", take);
            takeId = "REAPER:" + takeId;

            auto takeName = getReaperObjectString(
                getSetMediaItemTakeInfoString, take, "P_NAME");
            if (takeName.isEmpty())
                takeName = audioSourceName;
            if (takeName.isEmpty())
                takeName = "TAKE " + juce::String(takeIndex + 1);

            auto sourceName = activeSourceName;
            if (sourceName.isEmpty())
                sourceName = "SOURCE " + juce::String(itemIndex + 1);

            const auto playRate = std::max(
                0.000001, getMediaItemTakeInfoValue(take, "D_PLAYRATE"));
            choices.push_back({ itemId,
                                sourceName,
                                takeId,
                                takeName,
                                takeIndex + 1,
                                audioSourceName,
                                takePersistentId,
                                itemStart,
                                itemDuration,
                                getMediaItemTakeInfoValue(take, "D_STARTOFFS"),
                                playRate,
                                true,
                                take == activeTake });
        }
    }
#endif

    return choices;
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
            .withStringFromValueFunction([] (const float value, int)
            {
                return formatScopeTime(value);
            })));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { scopeStyleParameterId, 1 },
        "SCOPE / STYLE",
        juce::StringArray { "FILLED", "OUTLINE" },
        0,
        juce::AudioParameterChoiceAttributes().withAutomatable(false).withMeta(true)));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { scopeOpacityParameterId, 1 },
        "SCOPE / OPACITY",
        juce::NormalisableRange<float> { 10.0f, 100.0f, 1.0f },
        100.0f,
        juce::AudioParameterFloatAttributes()
            .withAutomatable(false)
            .withMeta(true)
            .withStringFromValueFunction([] (const float value, int)
            {
                return juce::String(juce::roundToInt(value));
            })));

    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { scopeZoomControlsParameterId, 1 },
        "SCOPE / ZOOM",
        true,
        juce::AudioParameterBoolAttributes().withAutomatable(false).withMeta(true)));

    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { scopeMonitorControlsParameterId, 1 },
        "SCOPE / MONITOR",
        true,
        juce::AudioParameterBoolAttributes().withAutomatable(false).withMeta(true)));

    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { scopeOtherControlsParameterId, 1 },
        "SCOPE / OTHERS",
        true,
        juce::AudioParameterBoolAttributes().withAutomatable(false).withMeta(true)));

    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { alwaysSecondTakeParameterId, 1 },
        "SCOPE / ALWAYS SECOND TAKE",
        false,
        juce::AudioParameterBoolAttributes().withAutomatable(false).withMeta(true)));

    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { offlineModeParameterId, 1 },
        "SCOPE / OFFLINE",
        false,
        juce::AudioParameterBoolAttributes().withAutomatable(false).withMeta(true)));

    const juce::StringArray scopeChannelModes { "LEFT", "RIGHT", "MID", "SIDE", "LR", "MS" };
    for (size_t bandIndex = 0; bandIndex < scopeChannelModeParameterIds.size(); ++bandIndex)
    {
        layout.add(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID { scopeChannelModeParameterIds[bandIndex], 1 },
            "SCOPE / BAND " + juce::String(static_cast<int>(bandIndex + 1)) + " / CHANNEL",
            scopeChannelModes,
            static_cast<int>(ana::ScopeChannelMode::mid),
            juce::AudioParameterChoiceAttributes().withAutomatable(false).withMeta(true)));

        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { scopeVerticalZoomParameterIds[bandIndex], 1 },
            "SCOPE / BAND " + juce::String(static_cast<int>(bandIndex + 1)) + " / ZOOM",
            juce::NormalisableRange<float> { -48.0f, 96.0f, 0.1f },
            0.0f,
            juce::AudioParameterFloatAttributes()
                .withAutomatable(false)
                .withMeta(true)
                .withStringFromValueFunction([] (const float value, int)
                {
                    const auto prefix = value > 0.05f ? "+" : "";
                    return prefix + juce::String(std::abs(value) < 0.05f ? 0.0f : value, 1);
                })));

        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID { scopeNormalizeParameterIds[bandIndex], 1 },
            "SCOPE / BAND " + juce::String(static_cast<int>(bandIndex + 1)) + " / NORMALIZE",
            false,
            juce::AudioParameterBoolAttributes().withAutomatable(false).withMeta(true)));
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

void AnaAudioProcessor::prepareToPlay(const double sampleRate, const int samplesPerBlock)
{
    multibandScope.prepare(sampleRate);

#if JucePlugin_Enable_ARA
    prepareToPlayForARA(sampleRate,
                        samplesPerBlock,
                        getMainBusNumOutputChannels(),
                        getProcessingPrecision());
#endif
}

void AnaAudioProcessor::releaseResources()
{
#if JucePlugin_Enable_ARA
    releaseResourcesForARA();
#endif

    multibandScope.reset();
}

bool AnaAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto input = layouts.getMainInputChannelSet();
    const auto output = layouts.getMainOutputChannelSet();

    return input == output
        && (output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo());
}

void AnaAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused(noDenormals);

    multibandScope.setCrossoverSettings(getActiveSplitCount(), getCrossoverFrequencies());

#if JucePlugin_Enable_ARA
    processBlockForARA(buffer, isRealtime(), getPlayHead());
#endif

    if (! isOfflineMode())
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
#if JucePlugin_Enable_ARA
    double tailLength = 0.0;

    if (getTailLengthSecondsForARA(tailLength))
        return tailLength;
#endif

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

void AnaAudioProcessor::setStateInformation(const void* data, const int sizeInBytes)
{
    if (auto state = getXmlFromBinary(data, sizeInBytes))
        if (state->hasTagName(parameters.state.getType()))
        {
            parameters.replaceState(juce::ValueTree::fromXml(*state));

            {
                const juce::ScopedLock scopedLock(offlineSelectionLock);
                selectedOfflineSourceId = parameters.state.getProperty(
                    offlineSourceStateKey, juce::String()).toString();
                selectedOfflineTakeId = parameters.state.getProperty(
                    offlineTakeStateKey, juce::String()).toString();
            }

            setLastEditorSize(
                static_cast<int>(parameters.state.getProperty(editorWidthStateKey, 0)),
                static_cast<int>(parameters.state.getProperty(editorHeightStateKey, 0)));
        }
}

bool AnaAudioProcessor::isOfflineMode() const noexcept
{
    if (const auto* value = parameters.getRawParameterValue(offlineModeParameterId))
        return value->load(std::memory_order_relaxed) >= 0.5f;

    return false;
}

bool AnaAudioProcessor::isARAAvailable() const noexcept
{
#if JucePlugin_Enable_ARA
    return isBoundToARA() && (isPlaybackRenderer() || isEditorRenderer());
#else
    return false;
#endif
}

void AnaAudioProcessor::requestOfflineAnalysis(const size_t columnCount, const bool forceRefresh)
{
#if JucePlugin_Enable_ARA
    const auto activeSplitCount = getActiveSplitCount();
    const auto frequencies = getCrossoverFrequencies();
    const auto sourceId = getSelectedOfflineSourceId();
    const auto takeId = getSelectedOfflineTakeId();
    const auto sourceTakeChoices = getOfflineSourceTakeChoices();

    if (auto* renderer = getPlaybackRenderer<ana::ara::PlaybackRenderer>())
        renderer->requestOfflineAnalysis(
            activeSplitCount, frequencies, columnCount, sourceId, takeId,
            sourceTakeChoices, forceRefresh);

    if (auto* renderer = getEditorRenderer<ana::ara::EditorRenderer>())
        renderer->requestOfflineAnalysis(
            activeSplitCount, frequencies, columnCount, sourceId, takeId,
            sourceTakeChoices, forceRefresh);
#else
    juce::ignoreUnused(columnCount, forceRefresh);
#endif
}

std::shared_ptr<const ana::OfflineScopeSnapshot> AnaAudioProcessor::getOfflineScopeSnapshot() const
{
#if JucePlugin_Enable_ARA
    std::shared_ptr<const ana::OfflineScopeSnapshot> playbackSnapshot;

    if (auto* renderer = getPlaybackRenderer<ana::ara::PlaybackRenderer>())
        playbackSnapshot = renderer->getOfflineSnapshot();

    if (auto* renderer = getEditorRenderer<ana::ara::EditorRenderer>())
    {
        auto editorSnapshot = renderer->getOfflineSnapshot();

        if (editorSnapshot != nullptr && editorSnapshot->durationSeconds > 0.0)
            return editorSnapshot;
    }

    return playbackSnapshot;
#endif

    return {};
}

std::vector<ana::OfflineSourceTakeChoice> AnaAudioProcessor::getOfflineSourceTakeChoices() const
{
    auto reaperChoices = getReaperSourceTakeChoices();
    if (! reaperChoices.empty())
        return reaperChoices;

#if JucePlugin_Enable_ARA
    if (auto* renderer = getEditorRenderer<ana::ara::EditorRenderer>())
    {
        auto choices = renderer->getOfflineSourceTakeChoices();

        if (! choices.empty())
            return choices;
    }

    if (auto* renderer = getPlaybackRenderer<ana::ara::PlaybackRenderer>())
        return renderer->getOfflineSourceTakeChoices();
#endif

    return {};
}

juce::String AnaAudioProcessor::getSelectedOfflineSourceId() const
{
    const juce::ScopedLock scopedLock(offlineSelectionLock);
    return selectedOfflineSourceId;
}

juce::String AnaAudioProcessor::getSelectedOfflineTakeId() const
{
    const juce::ScopedLock scopedLock(offlineSelectionLock);
    return selectedOfflineTakeId;
}

double AnaAudioProcessor::getScopeTimeMilliseconds() const noexcept
{
    if (const auto* value = parameters.getRawParameterValue(scopeTimeParameterId))
        return static_cast<double>(value->load(std::memory_order_relaxed));

    return 1000.0;
}

bool AnaAudioProcessor::isScopeFilledStyle() const noexcept
{
    if (const auto* value = parameters.getRawParameterValue(scopeStyleParameterId))
        return value->load(std::memory_order_relaxed) < 0.5f;

    return true;
}

float AnaAudioProcessor::getScopeOpacity() const noexcept
{
    if (const auto* value = parameters.getRawParameterValue(scopeOpacityParameterId))
        return juce::jlimit(0.1f, 1.0f, value->load(std::memory_order_relaxed) * 0.01f);

    return 1.0f;
}

bool AnaAudioProcessor::areScopeZoomControlsVisible() const noexcept
{
    if (const auto* value = parameters.getRawParameterValue(scopeZoomControlsParameterId))
        return value->load(std::memory_order_relaxed) >= 0.5f;

    return true;
}

bool AnaAudioProcessor::areScopeMonitorControlsVisible() const noexcept
{
    if (const auto* value = parameters.getRawParameterValue(scopeMonitorControlsParameterId))
        return value->load(std::memory_order_relaxed) >= 0.5f;

    return true;
}

bool AnaAudioProcessor::areScopeOtherControlsVisible() const noexcept
{
    if (const auto* value = parameters.getRawParameterValue(scopeOtherControlsParameterId))
        return value->load(std::memory_order_relaxed) >= 0.5f;

    return true;
}

bool AnaAudioProcessor::shouldAlwaysUseSecondOfflineTake() const noexcept
{
    if (const auto* value = parameters.getRawParameterValue(alwaysSecondTakeParameterId))
        return value->load(std::memory_order_relaxed) >= 0.5f;

    return false;
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
            const auto mode = juce::jlimit(0, 5, static_cast<int>(std::round(value->load(std::memory_order_relaxed))));
            return static_cast<ana::ScopeChannelMode>(mode);
        }
    }

    return ana::ScopeChannelMode::mid;
}

float AnaAudioProcessor::getScopeVerticalZoomDecibels(const size_t bandIndex) const noexcept
{
    if (bandIndex < scopeVerticalZoomParameterIds.size())
    {
        if (const auto* value = parameters.getRawParameterValue(scopeVerticalZoomParameterIds[bandIndex]))
        {
            return juce::jlimit(-48.0f, 96.0f, value->load(std::memory_order_relaxed));
        }
    }

    return 0.0f;
}

bool AnaAudioProcessor::isScopeBandNormalized(const size_t bandIndex) const noexcept
{
    if (bandIndex < scopeNormalizeParameterIds.size())
        if (const auto* value = parameters.getRawParameterValue(scopeNormalizeParameterIds[bandIndex]))
            return value->load(std::memory_order_relaxed) >= 0.5f;

    return false;
}

juce::Point<int> AnaAudioProcessor::getLastEditorSize() const noexcept
{
    return { lastEditorWidth.load(std::memory_order_relaxed),
             lastEditorHeight.load(std::memory_order_relaxed) };
}

int AnaAudioProcessor::getScopeSingleViewBand() const noexcept
{
    return juce::jlimit(-1, static_cast<int>(ana::MultibandScope::numBands) - 1,
                        static_cast<int>(parameters.state.getProperty(scopeSingleViewStateKey, -1)));
}

void AnaAudioProcessor::setLastEditorSize(const int width, const int height) noexcept
{
    const auto validWidth = std::max(0, width);
    const auto validHeight = std::max(0, height);

    if (lastEditorWidth.load(std::memory_order_relaxed) == validWidth
        && lastEditorHeight.load(std::memory_order_relaxed) == validHeight
        && static_cast<int>(parameters.state.getProperty(editorWidthStateKey, 0)) == validWidth
        && static_cast<int>(parameters.state.getProperty(editorHeightStateKey, 0)) == validHeight)
        return;

    lastEditorWidth.store(validWidth, std::memory_order_relaxed);
    lastEditorHeight.store(validHeight, std::memory_order_relaxed);

    if (validWidth > 0 && validHeight > 0)
    {
        parameters.state.setProperty(editorWidthStateKey, validWidth, nullptr);
        parameters.state.setProperty(editorHeightStateKey, validHeight, nullptr);
        updateHostDisplay(juce::AudioProcessorListener::ChangeDetails()
                              .withNonParameterStateChanged(true));
    }
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

void AnaAudioProcessor::setOfflineMode(const bool shouldUseOfflineMode)
{
    auto* parameter = parameters.getParameter(offlineModeParameterId);
    if (parameter == nullptr)
        return;

    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost(shouldUseOfflineMode ? 1.0f : 0.0f);
    parameter->endChangeGesture();
}

void AnaAudioProcessor::setSelectedOfflineSourceId(const juce::String& sourceId)
{
    {
        const juce::ScopedLock scopedLock(offlineSelectionLock);

        if (selectedOfflineSourceId == sourceId)
            return;

        selectedOfflineSourceId = sourceId;
    }

    if (sourceId.isEmpty())
        parameters.state.removeProperty(offlineSourceStateKey, nullptr);
    else
        parameters.state.setProperty(offlineSourceStateKey, sourceId, nullptr);

    updateHostDisplay(juce::AudioProcessorListener::ChangeDetails()
                          .withNonParameterStateChanged(true));
}

void AnaAudioProcessor::setSelectedOfflineTakeId(const juce::String& takeId)
{
    {
        const juce::ScopedLock scopedLock(offlineSelectionLock);

        if (selectedOfflineTakeId == takeId)
            return;

        selectedOfflineTakeId = takeId;
    }

    if (takeId.isEmpty())
        parameters.state.removeProperty(offlineTakeStateKey, nullptr);
    else
        parameters.state.setProperty(offlineTakeStateKey, takeId, nullptr);

    updateHostDisplay(juce::AudioProcessorListener::ChangeDetails()
                          .withNonParameterStateChanged(true));
}

void AnaAudioProcessor::setAlwaysUseSecondOfflineTake(const bool shouldUseSecondTake)
{
    auto* parameter = parameters.getParameter(alwaysSecondTakeParameterId);
    if (parameter == nullptr)
        return;

    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost(shouldUseSecondTake ? 1.0f : 0.0f);
    parameter->endChangeGesture();
}

void AnaAudioProcessor::setScopeSingleViewBand(const int bandIndex)
{
    const auto validBand = juce::jlimit(-1,
                                        static_cast<int>(ana::MultibandScope::numBands) - 1,
                                        bandIndex);

    if (getScopeSingleViewBand() == validBand)
        return;

    parameters.state.setProperty(scopeSingleViewStateKey, validBand, nullptr);
    updateHostDisplay(juce::AudioProcessorListener::ChangeDetails()
                          .withNonParameterStateChanged(true));
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

void AnaAudioProcessor::setScopeVerticalZoomDecibels(const size_t bandIndex, const float decibels)
{
    if (bandIndex >= scopeVerticalZoomParameterIds.size())
        return;

    auto* parameter = parameters.getParameter(scopeVerticalZoomParameterIds[bandIndex]);
    if (parameter == nullptr)
        return;

    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost(parameter->convertTo0to1(juce::jlimit(-48.0f, 96.0f, decibels)));
    parameter->endChangeGesture();
}

void AnaAudioProcessor::setScopeBandNormalized(const size_t bandIndex, const bool shouldNormalize)
{
    if (bandIndex >= scopeNormalizeParameterIds.size())
        return;

    auto* parameter = parameters.getParameter(scopeNormalizeParameterIds[bandIndex]);
    if (parameter == nullptr)
        return;

    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost(shouldNormalize ? 1.0f : 0.0f);
    parameter->endChangeGesture();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AnaAudioProcessor();
}
