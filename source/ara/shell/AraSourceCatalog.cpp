#include "AraSourceCatalog.h"
#include "shell/AudioFingerprint.h"

#if JucePlugin_Enable_ARA

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <string>

namespace ana::ara
{
namespace
{
juce::String normalizePersistentId(const juce::String& value)
{
    juce::String normalized;
    for (const auto character : value)
        if (juce::CharacterFunctions::getHexDigitValue(character) >= 0)
            normalized += juce::CharacterFunctions::toLowerCase(character);
    return normalized;
}

std::optional<uint64_t> getAudioContentFingerprint(juce::ARAAudioSource& source)
{
    const auto sampleRate = source.getSampleRate();
    const auto sourceSampleCount = source.getSampleCount();
    const auto channelCount = std::clamp(source.getChannelCount(), 1, 2);
    if (sampleRate <= 0.0 || sourceSampleCount <= 0)
        return {};

    auto hash = audio_fingerprint::addChannelCount(audio_fingerprint::initialHash, channelCount);

    juce::ARAAudioSourceReader reader(&source);
    juce::AudioBuffer<float> buffer(channelCount, audio_fingerprint::samplesPerSegment);
    for (const auto position : audio_fingerprint::positions)
    {
        const auto start = std::clamp<juce::int64>(
            static_cast<juce::int64>(std::llround(position * static_cast<double>(sourceSampleCount))),
            0,
            std::max<juce::int64>(0, sourceSampleCount - audio_fingerprint::samplesPerSegment));
        buffer.clear();
        if (! reader.read(&buffer, 0, audio_fingerprint::samplesPerSegment, start, true, true))
            return {};

        hash = audio_fingerprint::addSegment(
            hash, channelCount, [&buffer] (const int sample, const int channel)
            {
                return buffer.getSample(channel, sample);
            });
    }

    return hash;
}

juce::String getSourceId(const juce::ARAAudioSource* source, const void* fallback)
{
    if (source != nullptr && ! source->getPersistentID().empty())
        return "SRC:" + juce::String::fromUTF8(source->getPersistentID().c_str());

    return "SRC-RUNTIME:" + juce::String::toHexString(
        static_cast<juce::int64>(reinterpret_cast<intptr_t>(source != nullptr
                                                                 ? static_cast<const void*>(source)
                                                                 : fallback)));
}

juce::String getTakeId(const juce::ARAAudioModification* modification, const void* fallback)
{
    if (modification != nullptr && ! modification->getPersistentID().empty())
        return "MOD:" + juce::String::fromUTF8(modification->getPersistentID().c_str());

    return "MOD-RUNTIME:" + juce::String::toHexString(
        static_cast<juce::int64>(reinterpret_cast<intptr_t>(modification != nullptr
                                                                 ? static_cast<const void*>(modification)
                                                                 : fallback)));
}

juce::String getPlaybackItemId(const juce::ARAPlaybackRegion& playbackRegion)
{
    const auto startMicroseconds = static_cast<juce::int64>(std::llround(
        playbackRegion.getStartInPlaybackTime() * 1000000.0));
    return "ITEM:" + juce::String(startMicroseconds);
}

juce::String getLogicalSourceId(const juce::ARAPlaybackRegion& playbackRegion)
{
    return getPlaybackItemId(playbackRegion);
}

juce::String getLogicalTakeId(const juce::ARAPlaybackRegion& playbackRegion)
{
    const auto* modification = playbackRegion.getAudioModification();
    return getTakeId(modification, &playbackRegion);
}

int getTakeNumber(const juce::ARAPlaybackRegion& playbackRegion,
                         const std::vector<ara::SourceChoice>& choices)
{
    const auto sourceId = getLogicalSourceId(playbackRegion);
    const auto takeId = getLogicalTakeId(playbackRegion);

    const auto choice = std::find_if(choices.begin(), choices.end(),
        [&] (const auto& candidate)
        {
            return candidate.sourceId == sourceId && candidate.takeId == takeId;
        });

    return choice != choices.end() ? choice->takeNumber : 0;
}
}

std::vector<juce::ARAPlaybackRegion*> collectAnalysisRegions(
    ARA::PlugIn::DocumentController* documentController,
    const std::vector<juce::ARAPlaybackRegion*>& rendererRegions)
{
    std::vector<juce::ARAPlaybackRegion*> regions;
    std::set<juce::ARAPlaybackRegion*> seen;
    const auto append = [&regions, &seen] (juce::ARAPlaybackRegion* region)
    {
        if (region != nullptr && seen.insert(region).second)
            regions.push_back(region);
    };

    for (auto* region : rendererRegions)
        append(region);

    auto* document = documentController != nullptr ? documentController->getDocument() : nullptr;
    if (document != nullptr)
        for (auto* source : document->getAudioSources<juce::ARAAudioSource>())
            for (auto* modification : source->getAudioModifications<juce::ARAAudioModification>())
                for (auto* region : modification->getPlaybackRegions<juce::ARAPlaybackRegion>())
                    append(region);

    return regions;
}

std::vector<ara::SourceChoice> makeSourceChoices(
    const std::vector<juce::ARAPlaybackRegion*>& playbackRegions)
{
    std::vector<ara::SourceChoice> choices;
    std::set<std::string> seenChoices;
    std::map<juce::String, int> sourceLabelCounts;
    std::map<juce::String, juce::String> sourceLabels;
    std::map<juce::String, int> takeCountsBySource;

    const auto append = [&] (juce::ARAAudioSource* source,
                             juce::ARAAudioModification* modification,
                             juce::ARAPlaybackRegion* playbackRegion,
                             const juce::String& logicalSourceId)
    {
        if (source == nullptr || modification == nullptr)
            return;

        const auto sourceId = logicalSourceId.isNotEmpty()
            ? logicalSourceId
            : getSourceId(source, playbackRegion);
        const auto takeId = getTakeId(modification, playbackRegion);
        if (! seenChoices.insert((sourceId + "\n" + takeId).toStdString()).second)
            return;

        const char* utf8SourceName = source->getName();
        const char* utf8TakeName = modification->getEffectiveName();

        if (utf8TakeName == nullptr || *utf8TakeName == '\0')
            utf8TakeName = playbackRegion != nullptr ? playbackRegion->getEffectiveName() : nullptr;

        auto sourceName = juce::String();
        if (const auto existingLabel = sourceLabels.find(sourceId);
            existingLabel != sourceLabels.end())
            sourceName = existingLabel->second;
        auto takeName = utf8TakeName != nullptr
            ? juce::String::fromUTF8(utf8TakeName)
            : juce::String();

        if (sourceName.isEmpty())
        {
            const char* utf8RegionName = playbackRegion != nullptr
                ? playbackRegion->getEffectiveName()
                : nullptr;
            sourceName = utf8RegionName != nullptr
                ? juce::String::fromUTF8(utf8RegionName)
                : utf8SourceName != nullptr ? juce::String::fromUTF8(utf8SourceName)
                                            : juce::String();
            if (sourceName.isEmpty())
                sourceName = "SOURCE " + juce::String(static_cast<int>(sourceLabels.size() + 1));

            auto& sourceOccurrence = sourceLabelCounts[sourceName];
            ++sourceOccurrence;
            if (sourceOccurrence > 1)
                sourceName += " " + juce::String(sourceOccurrence);
            sourceLabels[sourceId] = sourceName;
        }

        if (takeName.isEmpty())
            takeName = "TAKE " + juce::String(static_cast<int>(choices.size() + 1));

        const auto takeNumber = ++takeCountsBySource[sourceId];
        choices.push_back({ sourceId, sourceName, takeId, takeName, takeNumber,
                            utf8SourceName != nullptr
                                ? juce::File(juce::String::fromUTF8(utf8SourceName)).getFileName()
                                : juce::String(),
                            juce::String::fromUTF8(source->getPersistentID().c_str()),
                            0.0, 0.0, 0.0, 1.0, 1.0, false, playbackRegion != nullptr });
    };

    auto sortedRegions = playbackRegions;
    std::stable_sort(sortedRegions.begin(), sortedRegions.end(),
        [] (const auto* left, const auto* right)
        {
            if (left == nullptr || right == nullptr)
                return left != nullptr && right == nullptr;
            const auto leftStart = left->getStartInPlaybackTime();
            const auto rightStart = right->getStartInPlaybackTime();
            if (leftStart < rightStart)
                return true;
            if (rightStart < leftStart)
                return false;
            const auto* leftSequence = left->getRegionSequence();
            const auto* rightSequence = right->getRegionSequence();
            const auto leftOrder = leftSequence != nullptr ? leftSequence->getOrderIndex() : 0;
            const auto rightOrder = rightSequence != nullptr ? rightSequence->getOrderIndex() : 0;
            if (leftOrder != rightOrder)
                return leftOrder < rightOrder;
            return getLogicalTakeId(*left).compare(getLogicalTakeId(*right)) < 0;
        });

    for (auto* playbackRegion : sortedRegions)
    {
        if (playbackRegion == nullptr)
            continue;

        auto* modification = playbackRegion->getAudioModification();
        append(modification != nullptr ? modification->getAudioSource() : nullptr,
               modification, playbackRegion, getLogicalSourceId(*playbackRegion));
    }

    return choices;
}

bool matchesSelection(const juce::ARAPlaybackRegion& playbackRegion,
                             const std::vector<ara::SourceChoice>& choices,
                             const juce::String& sourceId,
                             const int takeNumber)
{
    if (sourceId.isNotEmpty() && getLogicalSourceId(playbackRegion) != sourceId)
        return false;

    return takeNumber <= 0
        || getTakeNumber(playbackRegion, choices) == takeNumber;
}

juce::ARAAudioModification* findTakeModification(
    ARA::PlugIn::DocumentController* documentController,
    const std::vector<ara::SourceChoice>& choices,
    const juce::String& sourceId,
    const int takeNumber)
{
    auto* document = documentController != nullptr ? documentController->getDocument() : nullptr;
    if (document == nullptr || takeNumber <= 0)
        return nullptr;

    const auto selectedChoice = std::find_if(choices.begin(), choices.end(),
        [&] (const auto& choice)
        {
            return (sourceId.isEmpty() || choice.sourceId == sourceId)
                && choice.takeNumber == takeNumber;
        });
    if (selectedChoice == choices.end())
        return nullptr;

    for (auto* source : document->getAudioSources<juce::ARAAudioSource>())
        for (auto* modification : source->getAudioModifications<juce::ARAAudioModification>())
            if (getTakeId(modification, modification) == selectedChoice->takeId)
                return modification;

    return nullptr;
}

juce::ARAAudioSource* findHostTakeAudioSource(
    ARA::PlugIn::DocumentController* documentController,
    const ara::SourceChoice& choice)
{
    auto* document = documentController != nullptr ? documentController->getDocument() : nullptr;
    if (document == nullptr)
        return nullptr;

    const auto normalizedChoiceId = normalizePersistentId(choice.audioSourcePersistentId);
    if (normalizedChoiceId.isNotEmpty())
    {
        for (auto* source : document->getAudioSources<juce::ARAAudioSource>())
        {
            if (source != nullptr
                && normalizePersistentId(
                       juce::String::fromUTF8(source->getPersistentID().c_str()))
                    == normalizedChoiceId)
                return source;
        }
    }

    if (choice.hasAudioContentFingerprint)
    {
        for (auto* source : document->getAudioSources<juce::ARAAudioSource>())
            if (source != nullptr)
                if (const auto fingerprint = getAudioContentFingerprint(*source);
                    fingerprint.has_value() && *fingerprint == choice.audioContentFingerprint)
                    return source;
    }

    return nullptr;
}
}

#endif
