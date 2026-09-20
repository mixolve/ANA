#include "ReaperBridge.h"
#include "shell/AudioFingerprint.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <utility>

#if JucePlugin_Build_VST3
 #if defined(__clang__)
  #pragma clang diagnostic push
  #pragma clang diagnostic ignored "-Wshadow-field-in-constructor"
 #endif
#include <pluginterfaces/base/funknown.h>
 #if defined(__clang__)
  #pragma clang diagnostic pop
 #endif
#endif

namespace
{
#if JucePlugin_Build_VST3
struct ReaProject;
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

template <typename GetMediaSourceLength, typename GetMediaSourceSampleRate,
          typename GetMediaSourceNumChannels, typename GetPeaks>
std::optional<uint64_t> getReaperAudioContentFingerprint(
    PCM_source* source,
    const GetMediaSourceLength getMediaSourceLength,
    const GetMediaSourceSampleRate getMediaSourceSampleRate,
    const GetMediaSourceNumChannels getMediaSourceNumChannels,
    const GetPeaks getPeaks)
{
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

    auto hash = ana::audio_fingerprint::addChannelCount(
        ana::audio_fingerprint::initialHash, channelCount);

    std::array<double, ana::audio_fingerprint::samplesPerSegment * 2 * 2> peaks {};
    for (const auto position : ana::audio_fingerprint::positions)
    {
        const auto startTime = std::clamp(
            position * sourceLength,
            0.0,
            std::max(0.0, sourceLength
                - static_cast<double>(ana::audio_fingerprint::samplesPerSegment) / sampleRate));
        peaks.fill(0.0);
        const auto result = getPeaks(source, sampleRate, startTime, channelCount,
                                     ana::audio_fingerprint::samplesPerSegment, 0, peaks.data());
        const auto sampleCount = result & 0x000fffff;
        if (sampleCount != ana::audio_fingerprint::samplesPerSegment)
            return {};

        hash = ana::audio_fingerprint::addSegment(
            hash, channelCount, [&peaks, channelCount] (const int sample, const int channel)
            {
                return peaks[static_cast<size_t>(sample * channelCount + channel)];
            });
    }

    return hash;
}
#endif
}

ReaperHostBridge::ReaperHostBridge(ApplyTakeChoices applyTakeChoicesIn)
    : applyTakeChoices(std::move(applyTakeChoicesIn))
{
}

ReaperHostBridge::~ReaperHostBridge()
{
    stop();
    setHostApplication(nullptr);
}

void ReaperHostBridge::start()
{
    startTimerHz(10);
}

void ReaperHostBridge::stop()
{
    stopTimer();
}

void ReaperHostBridge::setHostApplication(Steinberg::FUnknown* newHostApplication)
{
#if JucePlugin_Build_VST3
    IReaperHostApplication* nextHost = nullptr;
    if (newHostApplication != nullptr)
    {
        void* queriedInterface = nullptr;
        if (newHostApplication->queryInterface(reaperHostApplicationIid, &queriedInterface)
                == Steinberg::kResultOk
            && queriedInterface != nullptr)
            nextHost = static_cast<IReaperHostApplication*>(queriedInterface);
    }

    {
        const juce::ScopedLock scopedLock(hostLock);
        auto* currentHost = static_cast<IReaperHostApplication*>(hostApplication);

        // Repeated host attachment may return the same referenced REAPER interface; preserve cached source state.
        if (nextHost == currentHost)
        {
            if (nextHost != nullptr)
                nextHost->release();
            return;
        }

        if (currentHost != nullptr)
            currentHost->release();
        hostApplication = nextHost;
    }
#else
    juce::ignoreUnused(newHostApplication);
#endif

    // Host-interface reattachment is not a media edit; retain the last valid catalogue and rescan semantically.
    lastProjectState.store(-1, std::memory_order_release);
    emptyScanConfirmations.store(0, std::memory_order_release);
}

int ReaperHostBridge::getProjectStateChangeCount() const
{
#if JucePlugin_Build_VST3
    const juce::ScopedLock scopedLock(hostLock);
    auto* host = static_cast<IReaperHostApplication*>(hostApplication);
    if (host == nullptr)
        return -1;

    using GetProjectStateChangeCount = int (*)(ReaProject*);
    const auto getProjectStateChangeCount = getReaperApi<GetProjectStateChangeCount>(
        *host, "GetProjectStateChangeCount");
    return getProjectStateChangeCount != nullptr ? getProjectStateChangeCount(nullptr) : -1;
#else
    return -1;
#endif
}

void ReaperHostBridge::timerCallback()
{
#if JucePlugin_Build_VST3
    const auto projectState = getProjectStateChangeCount();

    // A missing parent track during peer churn is temporary host unavailability, not an empty project.
    if (projectState < 0)
        return;

    const auto cachedProjectState = lastProjectState.load(std::memory_order_acquire);
    const auto cacheValid = takeChoicesCacheValid.load(std::memory_order_acquire);
    if (! cacheValid || projectState != cachedProjectState)
    {
        auto scannedChoices = scanTakeChoices();
        if (! scannedChoices.has_value())
            return;

        {
            const juce::ScopedLock scopedLock(choicesLock);

            // Debounce transient empty scans before accepting a destructive catalogue transition.
            if (scannedChoices->empty() && ! cachedTakeChoices.empty())
            {
                const auto confirmations = emptyScanConfirmations.fetch_add(
                    1, std::memory_order_acq_rel) + 1;
                if (confirmations < 3)
                    return;
            }
            else
            {
                emptyScanConfirmations.store(0, std::memory_order_release);
            }

            // A temporarily unavailable peak fingerprint must not become a semantic source change.
            for (auto& choice : *scannedChoices)
            {
                if (choice.hasAudioContentFingerprint)
                    continue;

                const auto previous = std::find_if(
                    cachedTakeChoices.begin(), cachedTakeChoices.end(),
                    [&choice] (const auto& candidate)
                    {
                        return candidate.sourceId == choice.sourceId
                            && candidate.takeId == choice.takeId;
                    });
                if (previous != cachedTakeChoices.end()
                    && previous->hasAudioContentFingerprint)
                {
                    choice.audioContentFingerprint = previous->audioContentFingerprint;
                    choice.hasAudioContentFingerprint = true;
                }
            }

            const auto choicesChanged = *scannedChoices != cachedTakeChoices;
            if (choicesChanged)
            {
                cachedTakeChoices = std::move(*scannedChoices);
                // Publish only semantic catalogue changes; REAPER project-state churn also covers editor bookkeeping.
                takeChoicesInitialised.store(false, std::memory_order_release);
            }
        }
        takeChoicesCacheValid.store(true, std::memory_order_release);
        lastProjectState.store(projectState, std::memory_order_release);
    }

    if (! takeChoicesInitialised.load(std::memory_order_acquire))
    {
        const auto choices = getTakeChoices();
        const auto initialised = applyTakeChoices != nullptr && applyTakeChoices(choices);
        takeChoicesInitialised.store(initialised, std::memory_order_release);
    }
#endif
}

std::optional<std::vector<ana::ara::SourceChoice>> ReaperHostBridge::scanTakeChoices() const
{
    std::vector<ana::ara::SourceChoice> choices;

#if JucePlugin_Build_VST3
    const juce::ScopedLock scopedLock(hostLock);
    auto* host = static_cast<IReaperHostApplication*>(hostApplication);
    if (host == nullptr)
        return std::nullopt;

    using CountTrackMediaItems = int (*)(MediaTrack*);
    using GetTrackMediaItem = MediaItem* (*)(MediaTrack*, int);
    using GetMediaItemNumTakes = int (*)(MediaItem*);
    using GetMediaItemTake = MediaItem_Take* (*)(MediaItem*, int);
    using GetActiveTake = MediaItem_Take* (*)(MediaItem*);
    using GetMediaItemTakeSource = PCM_source* (*)(MediaItem_Take*);
    using GetMediaSourceFileName = void (*)(PCM_source*, char*, int);
    using GetMediaSourceLength = double (*)(PCM_source*, bool*);
    using GetMediaSourceSampleRate = int (*)(PCM_source*);
    using GetMediaSourceNumChannels = int (*)(PCM_source*);
    using PCMSourceGetPeaks = int (*)(PCM_source*, double, double, int, int, int, double*);
    using GetMediaItemInfoValue = double (*)(MediaItem*, const char*);
    using GetMediaItemTakeInfoValue = double (*)(MediaItem_Take*, const char*);
    using GetSetMediaItemInfoString = bool (*)(MediaItem*, const char*, char*, bool);
    using GetSetMediaItemTakeInfoString = bool (*)(MediaItem_Take*, const char*, char*, bool);

    const auto countTrackMediaItems = getReaperApi<CountTrackMediaItems>(*host, "CountTrackMediaItems");
    const auto getTrackMediaItem = getReaperApi<GetTrackMediaItem>(*host, "GetTrackMediaItem");
    const auto getMediaItemNumTakes = getReaperApi<GetMediaItemNumTakes>(*host, "GetMediaItemNumTakes");
    const auto getMediaItemTake = getReaperApi<GetMediaItemTake>(*host, "GetMediaItemTake");
    const auto getActiveTake = getReaperApi<GetActiveTake>(*host, "GetActiveTake");
    const auto getMediaItemTakeSource = getReaperApi<GetMediaItemTakeSource>(*host, "GetMediaItemTake_Source");
    const auto getMediaSourceFileName = getReaperApi<GetMediaSourceFileName>(*host, "GetMediaSourceFileName");
    const auto getMediaSourceLength = getReaperApi<GetMediaSourceLength>(*host, "GetMediaSourceLength");
    const auto getMediaSourceSampleRate = getReaperApi<GetMediaSourceSampleRate>(*host, "GetMediaSourceSampleRate");
    const auto getMediaSourceNumChannels = getReaperApi<GetMediaSourceNumChannels>(*host, "GetMediaSourceNumChannels");
    const auto getPeaks = getReaperApi<PCMSourceGetPeaks>(*host, "PCM_Source_GetPeaks");
    const auto getMediaItemInfoValue = getReaperApi<GetMediaItemInfoValue>(*host, "GetMediaItemInfo_Value");
    const auto getMediaItemTakeInfoValue = getReaperApi<GetMediaItemTakeInfoValue>(*host, "GetMediaItemTakeInfo_Value");
    const auto getSetMediaItemInfoString = getReaperApi<GetSetMediaItemInfoString>(*host, "GetSetMediaItemInfo_String");
    const auto getSetMediaItemTakeInfoString = getReaperApi<GetSetMediaItemTakeInfoString>(*host, "GetSetMediaItemTakeInfo_String");

    auto* track = static_cast<MediaTrack*>(host->getReaperParent(1));
    if (track == nullptr || countTrackMediaItems == nullptr || getTrackMediaItem == nullptr
        || getMediaItemNumTakes == nullptr || getMediaItemTake == nullptr
        || getActiveTake == nullptr || getMediaItemTakeSource == nullptr
        || getMediaSourceFileName == nullptr || getMediaItemInfoValue == nullptr
        || getMediaItemTakeInfoValue == nullptr || getSetMediaItemInfoString == nullptr
        || getSetMediaItemTakeInfoString == nullptr)
        return std::nullopt;

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

std::vector<ana::ara::SourceChoice> ReaperHostBridge::getTakeChoices() const
{
    if (! takeChoicesCacheValid.load(std::memory_order_acquire))
        return {};

    const juce::ScopedLock scopedLock(choicesLock);
    return cachedTakeChoices;
}

