#include "Processor.h"
#include "Editor.h"
#include "../ara/EditorRenderer.h"
#include "../ara/PlaybackRenderer.h"

#include <algorithm>
#include <cmath>
#include <tuple>

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

uint64_t hashAudioSample(uint64_t hash, const double sample)
{
    const auto quantized = static_cast<uint32_t>(std::llround(
        std::clamp(sample, -1.0, 1.0) * 8388607.0));
    hash ^= quantized;
    return hash * 1099511628211ull;
}

template <typename GetMediaSourceLength, typename GetMediaSourceSampleRate,
          typename GetMediaSourceNumChannels, typename GetPeaks>
std::optional<uint64_t> getReaperAudioContentFingerprint(
    PCM_source* source,
    const GetMediaSourceLength getMediaSourceLength,
    const GetMediaSourceSampleRate getMediaSourceSampleRate,
    const GetMediaSourceNumChannels getMediaSourceNumChannels,
    const GetPeaks getPeaks)
{
    constexpr int samplesPerSegment = 32;
    constexpr std::array<double, 4> positions { 0.067, 0.283, 0.571, 0.853 };
    constexpr uint64_t initialHash = 1469598103934665603ull;

    if (source == nullptr || getMediaSourceLength == nullptr || getMediaSourceSampleRate == nullptr
        || getMediaSourceNumChannels == nullptr || getPeaks == nullptr)
        return {};

    bool lengthIsQN = false;
    const auto sourceLength = getMediaSourceLength(source, &lengthIsQN);
    const auto sampleRate = getMediaSourceSampleRate(source);
    const auto channelCount = std::clamp(getMediaSourceNumChannels(source), 1, 2);
    if (lengthIsQN || ! std::isfinite(sourceLength) || sourceLength <= 0.0
        || ! std::isfinite(sampleRate) || sampleRate <= 0.0)
        return {};

    uint64_t hash = initialHash;
    hash ^= static_cast<uint64_t>(channelCount);
    hash *= 1099511628211ull;

    std::array<double, samplesPerSegment * 2 * 2> peaks {};
    for (const auto position : positions)
    {
        const auto startTime = std::clamp(position * sourceLength,
                                          0.0,
                                          std::max(0.0, sourceLength
                                                        - static_cast<double>(samplesPerSegment) / sampleRate));
        peaks.fill(0.0);
        const auto result = getPeaks(source, sampleRate, startTime, channelCount,
                                     samplesPerSegment, 0, peaks.data());
        const auto sampleCount = result & 0x000fffff;
        if (sampleCount != samplesPerSegment)
            return {};

        for (int sample = 0; sample < samplesPerSegment; ++sample)
            for (int channel = 0; channel < channelCount; ++channel)
                hash = hashAudioSample(hash, peaks[static_cast<size_t>(sample * channelCount + channel)]);
    }

    return hash;
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

PluginProcessor::PluginProcessor()
    : AudioProcessor(BusesProperties()
          .withInput("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "ANA_PARAMETERS", createParameterLayout())
{
    startTimerHz(10);
}

PluginProcessor::~PluginProcessor()
{
    stopTimer();
    setIHostApplication(nullptr);
}

juce::VST3ClientExtensions* PluginProcessor::getVST3ClientExtensions()
{
    return this;
}

void PluginProcessor::setIHostApplication(Steinberg::FUnknown* hostApplication)
{
    const juce::ScopedLock scopedLock(reaperHostLock);
    realtimeTakeProjectState.store(-1, std::memory_order_release);
    realtimeTakeChoicesInitialised.store(false, std::memory_order_release);

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
        {
            reaperHostApplication = queriedInterface;
        }
    }
#else
    juce::ignoreUnused(hostApplication);
#endif
}

int PluginProcessor::getReaperProjectStateChangeCount() const
{
#if JucePlugin_Build_VST3
    const juce::ScopedLock scopedLock(reaperHostLock);
    auto* host = static_cast<IReaperHostApplication*>(reaperHostApplication);
    if (host == nullptr)
        return -1;

    using GetProjectStateChangeCount = int (*)(void*);
    const auto getProjectStateChangeCount = getReaperApi<GetProjectStateChangeCount>(
        *host, "GetProjectStateChangeCount");
    return getProjectStateChangeCount != nullptr ? getProjectStateChangeCount(nullptr) : -1;
#else
    return -1;
#endif
}

void PluginProcessor::updateRealtimeTakeChoices()
{
#if JucePlugin_Enable_ARA && JucePlugin_Build_VST3
    const auto projectState = getReaperProjectStateChangeCount();
    if (realtimeTakeChoicesInitialised.load(std::memory_order_acquire)
        && projectState == realtimeTakeProjectState.load(std::memory_order_acquire))
        return;

    if (auto* renderer = getPlaybackRenderer<ana::ara::PlaybackRenderer>())
    {
        const auto choices = getReaperSourceTakeChoices();
        const auto initialised = renderer->setRealtimeTakeChoices(choices);
        realtimeTakeChoicesInitialised.store(initialised, std::memory_order_release);
        realtimeTakeProjectState.store(initialised ? projectState : -1,
                                       std::memory_order_release);
    }
#endif
}

void PluginProcessor::timerCallback()
{
    updateRealtimeTakeChoices();
}

std::vector<ana::OfflineSourceTakeChoice> PluginProcessor::getReaperSourceTakeChoices() const
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
    using GetMediaSourceLength = double (*)(PCM_source*, bool*);
    using GetMediaSourceSampleRate = double (*)(PCM_source*);
    using GetMediaSourceNumChannels = int (*)(PCM_source*);
    using PCMSourceGetPeaks = int (*)(PCM_source*, double, double, int, int, int, double*);
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
    const auto getMediaSourceLength = getReaperApi<GetMediaSourceLength>(*host, "GetMediaSourceLength");
    const auto getMediaSourceSampleRate = getReaperApi<GetMediaSourceSampleRate>(
        *host, "GetMediaSourceSampleRate");
    const auto getMediaSourceNumChannels = getReaperApi<GetMediaSourceNumChannels>(
        *host, "GetMediaSourceNumChannels");
    const auto getPeaks = getReaperApi<PCMSourceGetPeaks>(*host, "PCM_Source_GetPeaks");
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
        const auto itemGainValue = getMediaItemInfoValue(item, "D_VOL");
        const auto itemGain = std::isfinite(itemGainValue) ? itemGainValue : 1.0;
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
            const auto audioContentFingerprint = getReaperAudioContentFingerprint(
                getMediaItemTakeSource(take), getMediaSourceLength, getMediaSourceSampleRate,
                getMediaSourceNumChannels, getPeaks);
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
            const auto takeGainValue = getMediaItemTakeInfoValue(take, "D_VOL");
            const auto takeGain = std::isfinite(takeGainValue) ? takeGainValue : 1.0;
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
                                itemGain * takeGain,
                                true,
                                take == activeTake,
                                audioContentFingerprint.value_or(0),
                                audioContentFingerprint.has_value() });
        }
    }
#endif

    return choices;
}

juce::AudioProcessorValueTreeState::ParameterLayout PluginProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto timeRange = juce::NormalisableRange<float> { 1000.0f, 30000.0f, 100.0f };
    timeRange.setSkewForCentre(5000.0f);
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { scopeTimeParameterId, 1 },
        "SCOPE / TIME",
        timeRange,
        10000.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction([] (const float value, int)
            {
                return formatScopeTime(value);
            })));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { scopeTimeBaseParameterId, 1 },
        "SCOPE / TIME BASE",
        juce::StringArray { "MS", "NOTE" },
        0,
        juce::AudioParameterChoiceAttributes().withAutomatable(false).withMeta(true)));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { scopeNoteLengthParameterId, 1 },
        "SCOPE / NOTE LENGTH",
        juce::StringArray { "1/16", "1/8", "1/4", "1/2", "1/1",
                            "2/1", "4/1", "8/1", "16/1" },
        4,
        juce::AudioParameterChoiceAttributes().withAutomatable(false).withMeta(true)));

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

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { frequencyBlockSizeParameterId, 1 },
        "FREQ / BLOCK SIZE",
        juce::StringArray { "512", "1024", "2048", "4096", "8192", "16384" },
        2,
        juce::AudioParameterChoiceAttributes().withAutomatable(false).withMeta(true)));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { frequencyOverlapParameterId, 1 },
        "FREQ / OVERLAP",
        juce::NormalisableRange<float> { 0.0f, 0.95f, 0.01f },
        0.75f,
        juce::AudioParameterFloatAttributes()
            .withAutomatable(false)
            .withMeta(true)
            .withStringFromValueFunction([] (const float value, int)
            {
                return juce::String(juce::roundToInt(value * 100.0f));
            })));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { frequencyAverageTimeParameterId, 1 },
        "FREQ / AVG TIME",
        juce::NormalisableRange<float> { 20.0f, 5000.0f, 1.0f },
        500.0f,
        juce::AudioParameterFloatAttributes().withAutomatable(false).withMeta(true)));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { frequencySmoothingParameterId, 1 },
        "FREQ / SMOOTHING",
        juce::NormalisableRange<float> { 0.0f, 100.0f, 1.0f },
        30.0f,
        juce::AudioParameterFloatAttributes().withAutomatable(false).withMeta(true)));

    for (const auto& [id, name, defaultValue] : std::array {
             std::tuple { frequencyFilledDisplayParameterId, "FREQ / FILLED DISPLAY", true },
             std::tuple { frequencySecondSpectrumParameterId, "FREQ / 2ND SPECTRUM", false },
             std::tuple { frequencyAntiAliasParameterId, "FREQ / ANTI ALIAS", true },
             std::tuple { frequencyRangesVisibleParameterId, "FREQ / RANGES", true },
             std::tuple { frequencyHostClearParameterId, "FREQ / HOST CLEAR", false },
             std::tuple { frequencyCursorReadoutParameterId, "FREQ / CURSOR", true },
             std::tuple { frequencyMonitorControlsParameterId, "FREQ / MONITOR", true },
             std::tuple { frequencyZoomControlsParameterId, "FREQ / ZOOM", true },
             std::tuple { frequencySplitViewParameterId, "FREQ / SPLIT", false } })
        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID { id, 1 }, name, defaultValue,
            juce::AudioParameterBoolAttributes().withAutomatable(false).withMeta(true)));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { frequencyChannelModeParameterId, 1 },
        "FREQ / MONITOR MODE", juce::StringArray { "ST", "LR", "L", "R", "MS", "M", "S" }, 0,
        juce::AudioParameterChoiceAttributes().withAutomatable(false).withMeta(true)));

    for (const auto& [id, name] : std::array {
             std::pair { frequencyFirstSpectrumTypeParameterId, "FREQ / 1ST SPEC TYPE" },
             std::pair { frequencySecondSpectrumTypeParameterId, "FREQ / 2ND SPEC TYPE" } })
        layout.add(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID { id, 1 }, name,
            juce::StringArray { "RTAVG", "MAX" }, 0,
            juce::AudioParameterChoiceAttributes().withAutomatable(false).withMeta(true)));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { frequencySlopeParameterId, 1 },
        "FREQ / SLOPE",
        juce::NormalisableRange<float> { -12.0f, 12.0f, 0.1f }, 4.5f,
        juce::AudioParameterFloatAttributes().withAutomatable(false).withMeta(true)));

    const auto addFrequencyRangeParameter = [&layout] (const char* id, const juce::String& name,
                                                         const float minimum, const float maximum,
                                                         const float defaultValue)
    {
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { id, 1 }, name,
            juce::NormalisableRange<float> { minimum, maximum, 0.01f }, defaultValue,
            juce::AudioParameterFloatAttributes().withAutomatable(false).withMeta(true)));
    };
    addFrequencyRangeParameter(frequencyLowParameterId, "FREQ / LOW", 20.0f, 20000.0f, 20.0f);
    addFrequencyRangeParameter(frequencyHighParameterId, "FREQ / HIGH", 20.0f, 20000.0f, 20000.0f);
    addFrequencyRangeParameter(frequencyRangeLowParameterId, "FREQ / RANGE LOW", -120.0f, 24.0f, -96.0f);
    addFrequencyRangeParameter(frequencyRangeHighParameterId, "FREQ / RANGE HIGH", -120.0f, 24.0f, 0.0f);

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { correlationBlockSizeParameterId, 1 },
        "CORR / BLOCK SIZE",
        juce::StringArray { "512", "1024", "2048", "4096", "8192", "16384" },
        2,
        juce::AudioParameterChoiceAttributes().withAutomatable(false).withMeta(true)));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { correlationOverlapParameterId, 1 },
        "CORR / OVERLAP",
        juce::NormalisableRange<float> { 0.0f, 0.95f, 0.01f },
        0.75f,
        juce::AudioParameterFloatAttributes()
            .withAutomatable(false)
            .withMeta(true)
            .withStringFromValueFunction([] (const float value, int)
            {
                return juce::String(juce::roundToInt(value * 100.0f));
            })));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { correlationSmoothingParameterId, 1 }, "CORR / SMOOTHING",
        juce::NormalisableRange<float> { 0.0f, 100.0f, 1.0f }, 30.0f,
        juce::AudioParameterFloatAttributes().withAutomatable(false).withMeta(true)));

    for (const auto& [id, name, minimum, maximum, defaultValue] : std::array {
             std::tuple { correlationAverageTimeParameterId, "CORR / AVG TIME", 20.0f, 5000.0f, 500.0f } })
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { id, 1 }, name,
            juce::NormalisableRange<float> { minimum, maximum, 1.0f },
            defaultValue,
            juce::AudioParameterFloatAttributes().withAutomatable(false).withMeta(true)));

    for (const auto& [id, name, defaultValue] : std::array {
             std::tuple { correlationFilledDisplayParameterId, "CORR / FILLED DISPLAY", true },
             std::tuple { correlationSecondSpectrumParameterId, "CORR / 2ND SPECTRUM", false },
             std::tuple { correlationHostClearParameterId, "CORR / HOST CLEAR", false },
             std::tuple { correlationRangesVisibleParameterId, "CORR / RANGES", true },
             std::tuple { correlationCursorReadoutParameterId, "CORR / CURSOR", true },
             std::tuple { correlationZoomControlsParameterId, "CORR / ZOOM", true } })
        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID { id, 1 }, name, defaultValue,
            juce::AudioParameterBoolAttributes().withAutomatable(false).withMeta(true)));

    for (const auto& [id, name] : std::array {
             std::pair { correlationFirstSpectrumTypeParameterId, "CORR / 1ST GRAPH TYPE" },
             std::pair { correlationSecondSpectrumTypeParameterId, "CORR / 2ND GRAPH TYPE" } })
        layout.add(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID { id, 1 }, name,
            juce::StringArray { "RTAVG", "MAX" }, 0,
            juce::AudioParameterChoiceAttributes().withAutomatable(false).withMeta(true)));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { correlationModeParameterId, 1 },
        "CORR / MODE", juce::StringArray { "PHASE", "AMPLITUDE" }, 0,
        juce::AudioParameterChoiceAttributes().withAutomatable(false).withMeta(true)));
    addFrequencyRangeParameter(correlationLowParameterId, "CORR / LOW", 20.0f, 20000.0f, 20.0f);
    addFrequencyRangeParameter(correlationHighParameterId, "CORR / HIGH", 20.0f, 20000.0f, 20000.0f);
    addFrequencyRangeParameter(correlationRangeLowParameterId, "CORR / RANGE LOW", -1.0f, 1.0f, -1.0f);
    addFrequencyRangeParameter(correlationRangeHighParameterId, "CORR / RANGE HIGH", -1.0f, 1.0f, 1.0f);

    layout.add(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID { levelMeterWidthParameterId, 1 },
        "LVL / METER WIDTH", 106, 180, 106,
        juce::AudioParameterIntAttributes().withAutomatable(false).withMeta(true)));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { levelPeakHighParameterId, 1 }, "LVL / PEAK/RMS HIGH",
        juce::NormalisableRange<float> { -99.0f, 12.0f, 0.1f }, 0.0f,
        juce::AudioParameterFloatAttributes().withAutomatable(false).withMeta(true)));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { levelPeakLowParameterId, 1 }, "LVL / PEAK/RMS LOW",
        juce::NormalisableRange<float> { -99.0f, 12.0f, 0.1f }, -60.0f,
        juce::AudioParameterFloatAttributes().withAutomatable(false).withMeta(true)));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { levelLufsHighParameterId, 1 }, "LVL / LUFS HIGH",
        juce::NormalisableRange<float> { -99.0f, 12.0f, 0.1f }, 0.0f,
        juce::AudioParameterFloatAttributes().withAutomatable(false).withMeta(true)));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { levelLufsLowParameterId, 1 }, "LVL / LUFS LOW",
        juce::NormalisableRange<float> { -99.0f, 12.0f, 0.1f }, -60.0f,
        juce::AudioParameterFloatAttributes().withAutomatable(false).withMeta(true)));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { levelRmsWindowParameterId, 1 },
        "LVL / RMS WINDOW", juce::NormalisableRange<float> { 10.0f, 3000.0f, 10.0f }, 300.0f,
        juce::AudioParameterFloatAttributes().withAutomatable(false).withMeta(true)));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { levelPeakHoldTimeParameterId, 1 },
        "LVL / PEAK HOLD", juce::NormalisableRange<float> { 0.0f, 10000.0f, 10.0f }, 1000.0f,
        juce::AudioParameterFloatAttributes().withAutomatable(false).withMeta(true)));

    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { levelHostResetParameterId, 1 }, "LVL / RESET HOST", false,
        juce::AudioParameterBoolAttributes().withAutomatable(false).withMeta(true)));

    for (const auto& [id, name] : std::array {
             std::pair { levelPeakVisibleParameterId, "LVL / PEAK RMS VISIBLE" },
             std::pair { levelLoudnessVisibleParameterId, "LVL / LOUDNESS VISIBLE" },
             std::pair { levelHistoryVisibleParameterId, "LVL / HISTORY VISIBLE" } })
        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID { id, 1 }, name, true,
            juce::AudioParameterBoolAttributes().withAutomatable(false).withMeta(true)));

    for (const auto& [id, name] : std::array {
             std::pair { levelHistoryMomentaryParameterId, "LVL / HISTORY MOMENTARY" },
             std::pair { levelHistoryShortTermParameterId, "LVL / HISTORY SHORT TERM" },
             std::pair { levelHistoryIntegratedParameterId, "LVL / HISTORY INTEGRATED" },
             std::pair { levelHistoryZoomParameterId, "LVL / HISTORY ZOOM" } })
        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID { id, 1 }, name, true,
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

void PluginProcessor::prepareToPlay(const double sampleRate, const int samplesPerBlock)
{
    multibandScope.prepare(sampleRate);
    frequencySpectrum.prepare(sampleRate);
    correlationProcessor.prepare(sampleRate);
    levelMeter.prepare(sampleRate, samplesPerBlock);

#if JucePlugin_Enable_ARA
    prepareToPlayForARA(sampleRate,
                        samplesPerBlock,
                        getMainBusNumOutputChannels(),
                        getProcessingPrecision());
#endif
}

void PluginProcessor::releaseResources()
{
#if JucePlugin_Enable_ARA
    releaseResourcesForARA();
#endif

    multibandScope.reset();
    frequencySpectrum.reset();
    correlationProcessor.reset();
    levelMeter.reset();
}

bool PluginProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto input = layouts.getMainInputChannelSet();
    const auto output = layouts.getMainOutputChannelSet();

    return input == output
        && (output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo());
}

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused(noDenormals);

    multibandScope.setCrossoverSettings(getActiveSplitCount(), getCrossoverFrequencies());

    auto* playHead = getPlayHead();
    auto hostIsPlaying = false;
    auto hostTransportStateAvailable = false;
    if (playHead != nullptr)
        if (const auto position = playHead->getPosition())
        {
            if (const auto bpm = position->getBpm(); bpm.hasValue()
                && std::isfinite(*bpm) && *bpm > 0.0)
                hostTempoBpm.store(*bpm, std::memory_order_relaxed);
            if (const auto isPlaying = position->getIsPlaying())
            {
                hostTransportStateAvailable = true;
                hostIsPlaying = isPlaying != 0;
            }
        }

    const auto* levelHostReset = parameters.getRawParameterValue(levelHostResetParameterId);
    if (levelHostReset != nullptr && levelHostReset->load(std::memory_order_relaxed) >= 0.5f
        && hostIsPlaying && ! hostWasPlaying)
        levelMeter.requestClear();
    const auto* rmsWindow = parameters.getRawParameterValue(levelRmsWindowParameterId);
    const auto* peakHoldTime = parameters.getRawParameterValue(levelPeakHoldTimeParameterId);
    const auto parameterEnabled = [this] (const char* parameterId)
    {
        const auto* value = parameters.getRawParameterValue(parameterId);
        return value == nullptr || value->load(std::memory_order_relaxed) >= 0.5f;
    };
    const ana::lvls::MeterProcessor::ProcessingOptions levelOptions {
        parameterEnabled(levelPeakVisibleParameterId),
        parameterEnabled(levelLoudnessVisibleParameterId),
        parameterEnabled(levelHistoryVisibleParameterId)
    };

    // Realtime metering must inspect the host stream, like a regular analyzer.
    // This includes REAPER's item gain, fades, envelopes, take processing and
    // upstream FX. ARA rendering happens only after every analyzer has read it.
    levelMeter.processBlock(buffer, rmsWindow != nullptr
        ? rmsWindow->load(std::memory_order_relaxed) : 300.0f,
        peakHoldTime != nullptr ? peakHoldTime->load(std::memory_order_relaxed) : 1000.0f,
        hostTransportStateAvailable && ! hostIsPlaying,
        levelOptions);

    if (! isOfflineMode())
        multibandScope.processBlock(buffer);

    static constexpr std::array<int, 6> frequencyBlockSizes { 512, 1024, 2048, 4096, 8192, 16384 };
    const auto parameterChoice = [this] (const char* parameterId, const int highestIndex)
    {
        const auto* value = parameters.getRawParameterValue(parameterId);
        return value != nullptr
            ? juce::jlimit(0, highestIndex, juce::roundToInt(value->load(std::memory_order_relaxed)))
            : 0;
    };
    const auto blockSizeIndex = parameterChoice(frequencyBlockSizeParameterId,
                                                static_cast<int>(frequencyBlockSizes.size()) - 1);
    const auto* overlap = parameters.getRawParameterValue(frequencyOverlapParameterId);
    const auto* averagingTime = parameters.getRawParameterValue(frequencyAverageTimeParameterId);
    const auto* hostClear = parameters.getRawParameterValue(frequencyHostClearParameterId);
    if (hostClear != nullptr && hostClear->load(std::memory_order_relaxed) >= 0.5f
        && hostIsPlaying && ! hostWasPlaying)
        frequencySpectrum.requestClear();
    frequencySpectrum.processBlock(buffer, frequencyBlockSizes[static_cast<size_t>(blockSizeIndex)],
                                   overlap != nullptr ? overlap->load(std::memory_order_relaxed) : 0.75f,
                                   averagingTime != nullptr ? averagingTime->load(std::memory_order_relaxed) : 500.0f);
    const auto correlationBlockSizeIndex = parameterChoice(correlationBlockSizeParameterId,
                                                           static_cast<int>(frequencyBlockSizes.size()) - 1);
    const auto* correlationOverlap = parameters.getRawParameterValue(correlationOverlapParameterId);
    const auto* correlationAveragingTime = parameters.getRawParameterValue(correlationAverageTimeParameterId);
    const auto* correlationHostClear = parameters.getRawParameterValue(correlationHostClearParameterId);
    if (correlationHostClear != nullptr && correlationHostClear->load(std::memory_order_relaxed) >= 0.5f
        && hostIsPlaying && ! hostWasPlaying)
        correlationProcessor.requestClear();
    correlationProcessor.processBlock(buffer,
                                      frequencyBlockSizes[static_cast<size_t>(correlationBlockSizeIndex)],
                                      correlationOverlap != nullptr
                                          ? correlationOverlap->load(std::memory_order_relaxed)
                                          : 0.75f,
                                      correlationAveragingTime != nullptr
                                          ? correlationAveragingTime->load(std::memory_order_relaxed)
                                          : 500.0f);

#if JucePlugin_Enable_ARA
    processBlockForARA(buffer, isRealtime(), playHead);
#endif

    hostWasPlaying = hostIsPlaying;

    for (auto channel = getTotalNumInputChannels(); channel < getTotalNumOutputChannels(); ++channel)
        buffer.clear(channel, 0, buffer.getNumSamples());
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
#if JucePlugin_Enable_ARA
    double tailLength = 0.0;

    if (getTailLengthSecondsForARA(tailLength))
        return tailLength;
#endif

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

bool PluginProcessor::isOfflineMode() const noexcept
{
    if (const auto* value = parameters.getRawParameterValue(offlineModeParameterId))
        return value->load(std::memory_order_relaxed) >= 0.5f;

    return false;
}

bool PluginProcessor::isARAAvailable() const noexcept
{
#if JucePlugin_Enable_ARA
    return isBoundToARA() && (isPlaybackRenderer() || isEditorRenderer());
#else
    return false;
#endif
}

int PluginProcessor::getOfflineAnalysisProgress() const noexcept
{
#if JucePlugin_Enable_ARA
    if (auto* renderer = getEditorRenderer<ana::ara::EditorRenderer>())
        return renderer->getAnalysisProgress();
    if (auto* renderer = getPlaybackRenderer<ana::ara::PlaybackRenderer>())
        return renderer->getAnalysisProgress();
#endif

    return 0;
}

void PluginProcessor::requestOfflineAnalysis(const size_t columnCount, const bool forceRefresh)
{
#if JucePlugin_Enable_ARA
    const auto activeSplitCount = getActiveSplitCount();
    const auto frequencies = getCrossoverFrequencies();
    const auto sourceId = getSelectedOfflineSourceId();
    const auto takeId = getSelectedOfflineTakeId();
    const auto sourceTakeChoices = getOfflineSourceTakeChoices();
    static constexpr std::array<int, 6> frequencyBlockSizes { 512, 1024, 2048, 4096, 8192, 16384 };
    const auto* blockSize = parameters.getRawParameterValue(frequencyBlockSizeParameterId);
    const auto blockSizeIndex = juce::jlimit(0, static_cast<int>(frequencyBlockSizes.size()) - 1,
                                             blockSize != nullptr
                                                 ? juce::roundToInt(blockSize->load(std::memory_order_relaxed))
                                                 : 3);
    const auto* overlap = parameters.getRawParameterValue(frequencyOverlapParameterId);
    const auto* averagingTime = parameters.getRawParameterValue(frequencyAverageTimeParameterId);
    const auto frequencyOverlap = overlap != nullptr ? overlap->load(std::memory_order_relaxed) : 0.75f;
    const auto frequencyAveragingTime = averagingTime != nullptr
        ? averagingTime->load(std::memory_order_relaxed)
        : 500.0f;
    const auto* correlationBlockSize = parameters.getRawParameterValue(correlationBlockSizeParameterId);
    const auto correlationBlockSizeIndex = juce::jlimit(0, static_cast<int>(frequencyBlockSizes.size()) - 1,
        correlationBlockSize != nullptr
            ? juce::roundToInt(correlationBlockSize->load(std::memory_order_relaxed)) : 3);
    const auto* correlationOverlapValue = parameters.getRawParameterValue(correlationOverlapParameterId);
    const auto* correlationAveragingTimeValue = parameters.getRawParameterValue(correlationAverageTimeParameterId);
    const auto correlationOverlap = correlationOverlapValue != nullptr
        ? correlationOverlapValue->load(std::memory_order_relaxed) : 0.75f;
    const auto correlationAveragingTime = correlationAveragingTimeValue != nullptr
        ? correlationAveragingTimeValue->load(std::memory_order_relaxed) : 500.0f;
    const auto analyzerPage = getAnalyzerPageState();
    const auto levelSectionEnabled = [this] (const char* parameterId)
    {
        const auto* value = parameters.getRawParameterValue(parameterId);
        return value == nullptr || value->load(std::memory_order_relaxed) >= 0.5f;
    };
    const auto includeLevelPeakRms = levelSectionEnabled(levelPeakVisibleParameterId);
    const auto includeLevelLoudness = levelSectionEnabled(levelLoudnessVisibleParameterId);
    const auto includeLevelHistory = levelSectionEnabled(levelHistoryVisibleParameterId);

    ana::OfflineAnalysisRequest request;
    request.activeSplitCount = std::min(activeSplitCount, ana::dsp::Crossover::numSplits);
    request.frequencies = frequencies;
    request.columnCount = std::max<size_t>(1, columnCount);
    request.frequencyBlockSize = frequencyBlockSizes[static_cast<size_t>(blockSizeIndex)];
    request.frequencyOverlap = frequencyOverlap;
    request.frequencyAveragingTimeMilliseconds = frequencyAveragingTime;
    request.correlationBlockSize = frequencyBlockSizes[static_cast<size_t>(correlationBlockSizeIndex)];
    request.correlationOverlap = correlationOverlap;
    request.correlationAveragingTimeMilliseconds = correlationAveragingTime;
    request.sourceId = sourceId;
    request.takeId = takeId;
    request.sourceTakeChoices = sourceTakeChoices;
    request.analyzerPage = juce::jlimit(0, 3, analyzerPage);
    request.levelOptions = { includeLevelPeakRms, includeLevelLoudness, includeLevelHistory };

    if (auto* renderer = getPlaybackRenderer<ana::ara::PlaybackRenderer>())
        renderer->requestOfflineAnalysis(request, forceRefresh);

    if (auto* renderer = getEditorRenderer<ana::ara::EditorRenderer>())
        renderer->requestOfflineAnalysis(std::move(request), forceRefresh);
#else
    juce::ignoreUnused(columnCount, forceRefresh);
#endif
}

std::shared_ptr<const ana::OfflineAnalysisSnapshot> PluginProcessor::getOfflineAnalysisSnapshot() const
{
#if JucePlugin_Enable_ARA
    std::shared_ptr<const ana::OfflineAnalysisSnapshot> playbackSnapshot;

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

std::vector<ana::OfflineSourceTakeChoice> PluginProcessor::getOfflineSourceTakeChoices() const
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

juce::String PluginProcessor::getSelectedOfflineSourceId() const
{
    const juce::ScopedLock scopedLock(offlineSelectionLock);
    return selectedOfflineSourceId;
}

juce::String PluginProcessor::getSelectedOfflineTakeId() const
{
    const juce::ScopedLock scopedLock(offlineSelectionLock);
    return selectedOfflineTakeId;
}

double PluginProcessor::getScopeTimeMilliseconds() const noexcept
{
    if (isScopeTimeNoteBased())
    {
        static constexpr std::array<double, 9> wholeNoteDivisors {
            16.0, 8.0, 4.0, 2.0, 1.0, 0.5, 0.25, 0.125, 0.0625
        };
        const auto* noteValue = parameters.getRawParameterValue(scopeNoteLengthParameterId);
        const auto noteIndex = noteValue != nullptr
            ? juce::jlimit(0, static_cast<int>(wholeNoteDivisors.size()) - 1,
                           juce::roundToInt(noteValue->load(std::memory_order_relaxed)))
            : 4;
        const auto bpm = std::max(1.0, hostTempoBpm.load(std::memory_order_relaxed));
        return 240000.0 / (bpm * wholeNoteDivisors[static_cast<size_t>(noteIndex)]);
    }

    if (const auto* value = parameters.getRawParameterValue(scopeTimeParameterId))
        return static_cast<double>(value->load(std::memory_order_relaxed));

    return 10000.0;
}

bool PluginProcessor::isScopeTimeNoteBased() const noexcept
{
    if (const auto* value = parameters.getRawParameterValue(scopeTimeBaseParameterId))
        return value->load(std::memory_order_relaxed) >= 0.5f;

    return false;
}

bool PluginProcessor::isScopeFilledStyle() const noexcept
{
    if (const auto* value = parameters.getRawParameterValue(scopeStyleParameterId))
        return value->load(std::memory_order_relaxed) < 0.5f;

    return true;
}

float PluginProcessor::getScopeOpacity() const noexcept
{
    if (const auto* value = parameters.getRawParameterValue(scopeOpacityParameterId))
        return juce::jlimit(0.1f, 1.0f, value->load(std::memory_order_relaxed) * 0.01f);

    return 1.0f;
}

bool PluginProcessor::areScopeZoomControlsVisible() const noexcept
{
    if (const auto* value = parameters.getRawParameterValue(scopeZoomControlsParameterId))
        return value->load(std::memory_order_relaxed) >= 0.5f;

    return true;
}

bool PluginProcessor::areScopeMonitorControlsVisible() const noexcept
{
    if (const auto* value = parameters.getRawParameterValue(scopeMonitorControlsParameterId))
        return value->load(std::memory_order_relaxed) >= 0.5f;

    return true;
}

bool PluginProcessor::areScopeOtherControlsVisible() const noexcept
{
    if (const auto* value = parameters.getRawParameterValue(scopeOtherControlsParameterId))
        return value->load(std::memory_order_relaxed) >= 0.5f;

    return true;
}

size_t PluginProcessor::getActiveSplitCount() const noexcept
{
    if (const auto* value = parameters.getRawParameterValue(activeSplitCountParameterId))
    {
        const auto count = static_cast<int>(std::round(value->load(std::memory_order_relaxed)));
        return static_cast<size_t>(juce::jlimit(0, static_cast<int>(ana::dsp::Crossover::numSplits), count));
    }

    return ana::dsp::Crossover::numSplits;
}

ana::ScopeChannelMode PluginProcessor::getScopeChannelMode(const size_t bandIndex) const noexcept
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

float PluginProcessor::getScopeVerticalZoomDecibels(const size_t bandIndex) const noexcept
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

bool PluginProcessor::isScopeBandNormalized(const size_t bandIndex) const noexcept
{
    if (bandIndex < scopeNormalizeParameterIds.size())
        if (const auto* value = parameters.getRawParameterValue(scopeNormalizeParameterIds[bandIndex]))
            return value->load(std::memory_order_relaxed) >= 0.5f;

    return false;
}

juce::Point<int> PluginProcessor::getLastEditorSize() const noexcept
{
    return { lastEditorWidth.load(std::memory_order_relaxed),
             lastEditorHeight.load(std::memory_order_relaxed) };
}

std::array<float, 3> PluginProcessor::getLevelPartWeights() const noexcept
{
    std::array<float, 3> weights {};
    auto total = 0.0f;

    for (size_t index = 0; index < weights.size(); ++index)
    {
        const auto stored = static_cast<float>(parameters.state.getProperty(
            levelPartWeightStateKeys[index], 1.0));
        weights[index] = std::isfinite(stored) && stored > 0.0f ? stored : 1.0f;
        total += weights[index];
    }

    if (total <= 0.0f)
        return { 1.0f, 1.0f, 1.0f };

    for (auto& weight : weights)
        weight /= total;

    return weights;
}

int PluginProcessor::getScopeSingleViewBand() const noexcept
{
    return juce::jlimit(-1, static_cast<int>(ana::MultibandScope::numBands) - 1,
                        static_cast<int>(parameters.state.getProperty(scopeSingleViewStateKey, -1)));
}

bool PluginProcessor::isScopeFullSourceView() const noexcept
{
    return static_cast<bool>(parameters.state.getProperty(scopeFullSourceStateKey, false));
}

int PluginProcessor::getAnalyzerPageState() const noexcept
{
    return juce::jlimit(0, 3, static_cast<int>(parameters.state.getProperty(analyzerPageStateKey, 1)));
}

void PluginProcessor::setLastEditorSize(const int width, const int height) noexcept
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

void PluginProcessor::setLevelPartWeights(const std::array<float, 3>& weights)
{
    auto safeWeights = weights;
    auto total = 0.0f;
    for (auto& weight : safeWeights)
    {
        weight = std::isfinite(weight) ? std::max(0.001f, weight) : 1.0f;
        total += weight;
    }

    for (auto& weight : safeWeights)
        weight /= total;

    const auto current = getLevelPartWeights();
    auto changed = false;
    for (size_t index = 0; index < safeWeights.size(); ++index)
    {
        if (std::abs(current[index] - safeWeights[index]) <= 1.0e-5f)
            continue;

        parameters.state.setProperty(levelPartWeightStateKeys[index], safeWeights[index], nullptr);
        changed = true;
    }

    if (changed)
        updateHostDisplay(juce::AudioProcessorListener::ChangeDetails()
                              .withNonParameterStateChanged(true));
}

void PluginProcessor::setAnalyzerPageState(const int page)
{
    const auto safePage = juce::jlimit(0, 3, page);
    if (getAnalyzerPageState() == safePage)
        return;

    parameters.state.setProperty(analyzerPageStateKey, safePage, nullptr);
    updateHostDisplay(juce::AudioProcessorListener::ChangeDetails().withNonParameterStateChanged(true));
}

ana::dsp::Crossover::SplitFrequencies PluginProcessor::getCrossoverFrequencies() const noexcept
{
    ana::dsp::Crossover::SplitFrequencies frequencies {};

    for (size_t index = 0; index < frequencies.size(); ++index)
    {
        if (const auto* value = parameters.getRawParameterValue(crossoverParameterIds[index]))
            frequencies[index] = static_cast<double>(value->load(std::memory_order_relaxed));
    }

    return frequencies;
}

void PluginProcessor::setActiveSplitCount(const size_t splitCount)
{
    auto* parameter = parameters.getParameter(activeSplitCountParameterId);
    if (parameter == nullptr)
        return;

    const auto plainValue = static_cast<float>(std::min(splitCount, ana::dsp::Crossover::numSplits));
    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost(parameter->convertTo0to1(plainValue));
    parameter->endChangeGesture();
}

void PluginProcessor::setOfflineMode(const bool shouldUseOfflineMode)
{
    auto* parameter = parameters.getParameter(offlineModeParameterId);
    if (parameter == nullptr)
        return;

    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost(shouldUseOfflineMode ? 1.0f : 0.0f);
    parameter->endChangeGesture();
}

void PluginProcessor::setSelectedOfflineSourceId(const juce::String& sourceId)
{
    setOfflineSelection(selectedOfflineSourceId, offlineSourceStateKey, sourceId);
}

void PluginProcessor::setSelectedOfflineTakeId(const juce::String& takeId)
{
    setOfflineSelection(selectedOfflineTakeId, offlineTakeStateKey, takeId);
}

void PluginProcessor::setOfflineSelection(juce::String& currentValue,
                                          const char* stateKey,
                                          const juce::String& newValue)
{
    {
        const juce::ScopedLock scopedLock(offlineSelectionLock);

        if (currentValue == newValue)
            return;

        currentValue = newValue;
    }

    if (newValue.isEmpty())
        parameters.state.removeProperty(stateKey, nullptr);
    else
        parameters.state.setProperty(stateKey, newValue, nullptr);

    updateHostDisplay(juce::AudioProcessorListener::ChangeDetails()
                          .withNonParameterStateChanged(true));
}

void PluginProcessor::setScopeSingleViewBand(const int bandIndex)
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

void PluginProcessor::setScopeFullSourceView(const bool shouldShowFullSource)
{
    if (isScopeFullSourceView() == shouldShowFullSource)
        return;

    parameters.state.setProperty(scopeFullSourceStateKey, shouldShowFullSource, nullptr);
    if (shouldShowFullSource)
        parameters.state.setProperty(scopeSingleViewStateKey, -1, nullptr);

    updateHostDisplay(juce::AudioProcessorListener::ChangeDetails()
                          .withNonParameterStateChanged(true));
}

void PluginProcessor::setScopeChannelMode(const size_t bandIndex, const ana::ScopeChannelMode mode)
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

void PluginProcessor::setScopeVerticalZoomDecibels(const size_t bandIndex, const float decibels)
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

void PluginProcessor::setScopeBandNormalized(const size_t bandIndex, const bool shouldNormalize)
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
    return new PluginProcessor();
}
