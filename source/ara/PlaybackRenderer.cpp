#include "PlaybackRenderer.h"
#include "BandlimitedResampler.h"

#include "../corr/StereoProcessor.h"
#include "../freq/SpectrumProcessor.h"
#include "../lvls/MeterProcessor.h"

#if JucePlugin_Enable_ARA

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

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

uint64_t hashAudioSample(uint64_t hash, const float sample)
{
    const auto quantized = static_cast<uint32_t>(std::llround(
        std::clamp(static_cast<double>(sample), -1.0, 1.0) * 8388607.0));
    hash ^= quantized;
    return hash * 1099511628211ull;
}

std::optional<uint64_t> getAraAudioContentFingerprint(juce::ARAAudioSource& source)
{
    constexpr int samplesPerSegment = 32;
    constexpr std::array<double, 4> positions { 0.067, 0.283, 0.571, 0.853 };
    constexpr uint64_t initialHash = 1469598103934665603ull;

    const auto sampleRate = source.getSampleRate();
    const auto sourceSampleCount = source.getSampleCount();
    const auto channelCount = std::clamp(source.getChannelCount(), 1, 2);
    if (sampleRate <= 0.0 || sourceSampleCount <= 0)
        return {};

    uint64_t hash = initialHash;
    hash ^= static_cast<uint64_t>(channelCount);
    hash *= 1099511628211ull;

    juce::ARAAudioSourceReader reader(&source);
    juce::AudioBuffer<float> buffer(channelCount, samplesPerSegment);
    for (const auto position : positions)
    {
        const auto start = std::clamp<juce::int64>(
            static_cast<juce::int64>(std::llround(position * static_cast<double>(sourceSampleCount))),
            0,
            std::max<juce::int64>(0, sourceSampleCount - samplesPerSegment));
        buffer.clear();
        if (! reader.read(&buffer, 0, samplesPerSegment, start, true, true))
            return {};

        for (int sample = 0; sample < samplesPerSegment; ++sample)
            for (int channel = 0; channel < channelCount; ++channel)
                hash = hashAudioSample(hash, buffer.getSample(channel, sample));
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
} // namespace

juce::String getOfflineSourceId(const juce::ARAPlaybackRegion& playbackRegion)
{
    return getPlaybackItemId(playbackRegion);
}

juce::String getOfflineTakeId(const juce::ARAPlaybackRegion& playbackRegion)
{
    const auto* modification = playbackRegion.getAudioModification();

    return getTakeId(modification, &playbackRegion);
}

int getOfflineTakeNumber(const juce::ARAPlaybackRegion& playbackRegion,
                         const std::vector<OfflineSourceTakeChoice>& choices)
{
    const auto sourceId = getOfflineSourceId(playbackRegion);
    const auto takeId = getOfflineTakeId(playbackRegion);

    const auto choice = std::find_if(choices.begin(), choices.end(),
        [&] (const auto& candidate)
        {
            return candidate.sourceId == sourceId && candidate.takeId == takeId;
        });

    return choice != choices.end() ? choice->takeNumber : 0;
}

bool matchesOfflineSelection(const juce::ARAPlaybackRegion& playbackRegion,
                             const std::vector<OfflineSourceTakeChoice>& choices,
                             const juce::String& sourceId,
                             const juce::String& takeSelection)
{
    if (sourceId.isNotEmpty() && getOfflineSourceId(playbackRegion) != sourceId)
        return false;

    const auto selectedTakeNumber = takeSelection.getIntValue();
    return selectedTakeNumber <= 0
        || getOfflineTakeNumber(playbackRegion, choices) == selectedTakeNumber;
}

void prepareOfflineFrequencySpectrum(OfflineAnalysisSnapshot& snapshot,
                                     const int blockSize,
                                     const float overlap,
                                     const float averagingTimeMilliseconds)
{
    snapshot.spectrum = std::make_shared<freq::SpectrumProcessor>();
    snapshot.frequencySampleRate = 0.0;
    snapshot.frequencyBlockSize = blockSize;
    snapshot.frequencyOverlap = overlap;
    snapshot.frequencyAveragingTimeMilliseconds = averagingTimeMilliseconds;
}

void processOfflineFrequencyBlock(OfflineAnalysisSnapshot& snapshot,
                                  juce::AudioBuffer<float>& buffer,
                                  const int samplesToProcess,
                                  const double sampleRate)
{
    if (snapshot.spectrum == nullptr || sampleRate <= 0.0 || samplesToProcess <= 0)
        return;

    if (snapshot.frequencySampleRate <= 0.0)
    {
        snapshot.frequencySampleRate = sampleRate;
        snapshot.spectrum->prepare(sampleRate);
    }

    if (! juce::approximatelyEqual(snapshot.frequencySampleRate, sampleRate))
        return;

    if (samplesToProcess < buffer.getNumSamples())
        buffer.clear(samplesToProcess, buffer.getNumSamples() - samplesToProcess);

    snapshot.spectrum->processBlock(buffer, snapshot.frequencyBlockSize,
                                             snapshot.frequencyOverlap,
                                             snapshot.frequencyAveragingTimeMilliseconds);
}

void prepareOfflineCorrelationSpectrum(OfflineAnalysisSnapshot& snapshot,
                                       const int blockSize,
                                       const float overlap,
                                       const float averagingTimeMilliseconds)
{
    snapshot.correlation = std::make_shared<corr::StereoProcessor>();
    snapshot.correlationSampleRate = 0.0;
    snapshot.correlationBlockSize = blockSize;
    snapshot.correlationOverlap = overlap;
    snapshot.correlationAveragingTimeMilliseconds = averagingTimeMilliseconds;
}

void processOfflineCorrelationBlock(OfflineAnalysisSnapshot& snapshot,
                                    juce::AudioBuffer<float>& buffer,
                                    const int samplesToProcess,
                                    const double sampleRate)
{
    if (snapshot.correlation == nullptr || sampleRate <= 0.0 || samplesToProcess <= 0)
        return;

    if (snapshot.correlationSampleRate <= 0.0)
    {
        snapshot.correlationSampleRate = sampleRate;
        snapshot.correlation->prepare(sampleRate);
    }

    if (! juce::approximatelyEqual(snapshot.correlationSampleRate, sampleRate))
        return;

    if (samplesToProcess < buffer.getNumSamples())
        buffer.clear(samplesToProcess, buffer.getNumSamples() - samplesToProcess);

    snapshot.correlation->processBlock(buffer, snapshot.correlationBlockSize,
                                               snapshot.correlationOverlap,
                                               snapshot.correlationAveragingTimeMilliseconds);
}

void prepareOfflineLevelMeter(OfflineAnalysisSnapshot& snapshot)
{
    snapshot.meters = std::make_shared<lvls::MeterProcessor>();
    snapshot.levelSampleRate = 0.0;
}

void processOfflineLevelBlock(OfflineAnalysisSnapshot& snapshot,
                              juce::AudioBuffer<float>& buffer,
                              const int samplesToProcess,
                              const double sampleRate,
                              const lvls::MeterProcessor::ProcessingOptions options)
{
    if (snapshot.meters == nullptr || sampleRate <= 0.0 || samplesToProcess <= 0)
        return;

    if (snapshot.levelSampleRate <= 0.0)
    {
        snapshot.levelSampleRate = sampleRate;
        snapshot.meters->prepare(sampleRate);
    }

    if (! juce::approximatelyEqual(snapshot.levelSampleRate, sampleRate))
        return;

    if (samplesToProcess < buffer.getNumSamples())
        buffer.clear(samplesToProcess, buffer.getNumSamples() - samplesToProcess);

    snapshot.meters->processBlock(buffer, 300.0f, 1000.0f, false, options);
}

std::vector<juce::ARAPlaybackRegion*> collectOfflinePlaybackRegions(
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

std::vector<OfflineSourceTakeChoice> makeOfflineSourceTakeChoices(
    ARA::PlugIn::DocumentController* documentController,
    const std::vector<juce::ARAPlaybackRegion*>& playbackRegions)
{
    juce::ignoreUnused(documentController);
    std::vector<OfflineSourceTakeChoice> choices;
    std::set<std::string> seenChoices;
    std::map<juce::String, int> sourceLabelCounts;
    std::map<juce::String, juce::String> sourceLabels;
    std::map<juce::String, int> sourceTakeCounts;

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

        const char* utf8SourceName = source != nullptr ? source->getName() : nullptr;
        const char* utf8TakeName = modification != nullptr ? modification->getEffectiveName() : nullptr;

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

        const auto takeNumber = ++sourceTakeCounts[sourceId];
        choices.push_back({ sourceId, sourceName, takeId, takeName, takeNumber,
                            utf8SourceName != nullptr
                                ? juce::File(juce::String::fromUTF8(utf8SourceName)).getFileName()
                                : juce::String(),
                            source != nullptr
                                ? juce::String::fromUTF8(source->getPersistentID().c_str())
                                : juce::String(),
                            0.0, 0.0, 0.0, 1.0, 1.0, false, playbackRegion != nullptr });
    };

    auto sortedRegions = playbackRegions;
    std::stable_sort(sortedRegions.begin(), sortedRegions.end(),
        [] (const auto* left, const auto* right)
        {
            if (left == nullptr || right == nullptr)
                return right == nullptr;
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
            return getOfflineTakeId(*left).compare(getOfflineTakeId(*right)) < 0;
        });

    for (auto* playbackRegion : sortedRegions)
    {
        if (playbackRegion == nullptr)
            continue;

        auto* modification = playbackRegion->getAudioModification();
        append(modification != nullptr ? modification->getAudioSource() : nullptr,
               modification, playbackRegion, getOfflineSourceId(*playbackRegion));
    }

    return choices;
}

juce::ARAAudioModification* findOfflineTakeModification(
    ARA::PlugIn::DocumentController* documentController,
    const std::vector<OfflineSourceTakeChoice>& choices,
    const juce::String& sourceId,
    const juce::String& takeSelection)
{
    auto* document = documentController != nullptr ? documentController->getDocument() : nullptr;
    const auto selectedTakeNumber = takeSelection.getIntValue();
    if (document == nullptr || selectedTakeNumber <= 0)
        return nullptr;

    const auto selectedChoice = std::find_if(choices.begin(), choices.end(),
        [&] (const auto& choice)
        {
            return (sourceId.isEmpty() || choice.sourceId == sourceId)
                && choice.takeNumber == selectedTakeNumber;
        });
    if (selectedChoice == choices.end())
        return nullptr;

    for (auto* source : document->getAudioSources<juce::ARAAudioSource>())
    {
        for (auto* modification : source->getAudioModifications<juce::ARAAudioModification>())
            if (getTakeId(modification, modification) == selectedChoice->takeId)
                return modification;
    }

    return nullptr;
}

bool analyseOfflineTakeSource(
    OfflineAnalysisSnapshot& snapshot,
    juce::ARAAudioSource& source,
    const dsp::Crossover::SplitFrequencies& frequencies,
    const size_t activeSplitCount,
    const bool includeScope,
    const bool includeFrequency,
    const bool includeCorrelation,
    const bool includeLevel,
    const lvls::MeterProcessor::ProcessingOptions levelOptions,
    const std::function<bool()>& shouldCancel,
    const std::function<void(float)>& onProgress)
{
    constexpr int readBlockSize = 4096;
    const auto sourceRate = source.getSampleRate();
    const auto sourceSampleCount = source.getSampleCount();
    const auto columnCount = includeScope
        ? snapshot.bands.front().front().minimums.size() : size_t { 1 };

    if (sourceRate <= 0.0 || sourceSampleCount <= 0 || columnCount == 0)
        return true;

    juce::ARAAudioSourceReader reader(&source);
    juce::AudioBuffer<float> readBuffer(2, readBlockSize);
    dsp::Crossover crossover;
    crossover.prepare(sourceRate);
    crossover.setActiveSplitCount(activeSplitCount);
    crossover.setSplitFrequencies(frequencies);

    for (juce::int64 readPosition = 0;
         readPosition < sourceSampleCount;
         readPosition += readBlockSize)
    {
        if (shouldCancel())
            return false;

        const auto samplesToRead = static_cast<int>(std::min<juce::int64>(
            readBlockSize, sourceSampleCount - readPosition));
        if (! reader.read(&readBuffer, 0, samplesToRead, readPosition, true, true))
            continue;

        if (includeFrequency)
            processOfflineFrequencyBlock(snapshot, readBuffer, samplesToRead, sourceRate);
        if (includeCorrelation)
            processOfflineCorrelationBlock(snapshot, readBuffer, samplesToRead, sourceRate);
        if (includeLevel)
            processOfflineLevelBlock(snapshot, readBuffer, samplesToRead, sourceRate, levelOptions);
        onProgress(static_cast<float>(readPosition + samplesToRead)
                   / static_cast<float>(sourceSampleCount));

        if (! includeScope)
            continue;

        const auto* left = readBuffer.getReadPointer(0);
        const auto* right = readBuffer.getReadPointer(1);

        for (int sampleIndex = 0; sampleIndex < samplesToRead; ++sampleIndex)
        {
            const auto normalizedTime = static_cast<double>(readPosition + sampleIndex)
                / static_cast<double>(sourceSampleCount);
            const auto column = std::min(columnCount - 1,
                static_cast<size_t>(normalizedTime * static_cast<double>(columnCount)));
            const std::array<float, OfflineAnalysisSnapshot::numChannelModes> widebandModes {
                left[sampleIndex], right[sampleIndex],
                0.5f * (left[sampleIndex] + right[sampleIndex]),
                0.5f * (left[sampleIndex] - right[sampleIndex])
            };

            for (size_t modeIndex = 0; modeIndex < widebandModes.size(); ++modeIndex)
            {
                auto& envelope = snapshot.wideband[modeIndex];
                envelope.minimums[column] = std::min(envelope.minimums[column], widebandModes[modeIndex]);
                envelope.maximums[column] = std::max(envelope.maximums[column], widebandModes[modeIndex]);
            }
            const auto ranges = crossover.processSample(left[sampleIndex], right[sampleIndex]);

            for (size_t bandIndex = 0; bandIndex < snapshot.activeBandCount; ++bandIndex)
            {
                const auto bandLeft = static_cast<float>(ranges[bandIndex].left);
                const auto bandRight = static_cast<float>(ranges[bandIndex].right);
                const std::array<float, OfflineAnalysisSnapshot::numChannelModes> modes {
                    bandLeft,
                    bandRight,
                    0.5f * (bandLeft + bandRight),
                    0.5f * (bandLeft - bandRight)
                };

                for (size_t modeIndex = 0; modeIndex < modes.size(); ++modeIndex)
                {
                    auto& envelope = snapshot.bands[bandIndex][modeIndex];
                    envelope.minimums[column] = std::min(envelope.minimums[column], modes[modeIndex]);
                    envelope.maximums[column] = std::max(envelope.maximums[column], modes[modeIndex]);
                }
            }
        }
    }

    return true;
}

static juce::ARAAudioSource* findHostTakeAudioSource(
    ARA::PlugIn::DocumentController* documentController,
    const OfflineSourceTakeChoice& choice)
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
                if (const auto fingerprint = getAraAudioContentFingerprint(*source);
                    fingerprint.has_value() && *fingerprint == choice.audioContentFingerprint)
                    return source;
    }

    return nullptr;
}

static void initialiseOfflineEnvelopes(OfflineAnalysisSnapshot& snapshot, const size_t columnCount)
{
    const auto initialise = [columnCount] (auto& envelopes)
    {
        for (auto& envelope : envelopes)
        {
            envelope.minimums.assign(columnCount, std::numeric_limits<float>::max());
            envelope.maximums.assign(columnCount, std::numeric_limits<float>::lowest());
        }
    };

    for (auto& band : snapshot.bands)
        initialise(band);
    initialise(snapshot.wideband);
}

static void finishOfflineEnvelopes(OfflineAnalysisSnapshot& snapshot)
{
    const auto finish = [] (auto& envelopes)
    {
        for (auto& envelope : envelopes)
        {
            for (size_t column = 0; column < envelope.minimums.size(); ++column)
            {
                if (envelope.minimums[column] > envelope.maximums[column])
                    envelope.minimums[column] = envelope.maximums[column] = 0.0f;
            }
        }
    };

    for (auto& band : snapshot.bands)
        finish(band);
    finish(snapshot.wideband);
}

static bool analyseHostTakeChoice(
    OfflineAnalysisSnapshot& snapshot,
    juce::ARAAudioSource& source,
    const OfflineSourceTakeChoice& choice,
    const dsp::Crossover::SplitFrequencies& frequencies,
    const size_t activeSplitCount,
    const bool includeScope,
    const bool includeFrequency,
    const bool includeCorrelation,
    const bool includeLevel,
    const lvls::MeterProcessor::ProcessingOptions levelOptions,
    const std::function<bool()>& shouldCancel,
    const std::function<void(float)>& onProgress)
{
    constexpr int readBlockSize = 4096;
    const auto sourceRate = source.getSampleRate();
    const auto sourceSampleCount = source.getSampleCount();
    const auto columnCount = includeScope
        ? snapshot.bands.front().front().minimums.size() : size_t { 1 };
    if (sourceRate <= 0.0 || sourceSampleCount <= 0 || columnCount == 0
        || choice.playbackDurationSeconds <= 0.0)
        return true;

    const auto requestedSourceStart = static_cast<juce::int64>(std::llround(
        choice.sourceStartSeconds * sourceRate));
    const auto wantedSourceLength = static_cast<juce::int64>(std::ceil(
        choice.playbackDurationSeconds * choice.playRate * sourceRate));
    const auto sourceStart = std::clamp<juce::int64>(
        requestedSourceStart, 0, sourceSampleCount);
    const auto sourceEnd = std::clamp<juce::int64>(
        requestedSourceStart + wantedSourceLength, 0, sourceSampleCount);
    if (sourceEnd <= sourceStart)
        return true;

    juce::ARAAudioSourceReader reader(&source);
    juce::AudioBuffer<float> readBuffer(2, readBlockSize);
    dsp::Crossover crossover;
    crossover.prepare(sourceRate);
    crossover.setActiveSplitCount(activeSplitCount);
    crossover.setSplitFrequencies(frequencies);
    const auto sourceSamplesPerPlaybackSecond = choice.playRate * sourceRate;

    for (auto readPosition = sourceStart; readPosition < sourceEnd; readPosition += readBlockSize)
    {
        if (shouldCancel())
            return false;

        const auto samplesToRead = static_cast<int>(std::min<juce::int64>(
            readBlockSize, sourceEnd - readPosition));
        if (! reader.read(&readBuffer, 0, samplesToRead, readPosition, true, true))
            continue;

        if (! juce::approximatelyEqual(choice.gain, 1.0))
            readBuffer.applyGain(0, samplesToRead, static_cast<float>(choice.gain));

        if (includeFrequency)
            processOfflineFrequencyBlock(snapshot, readBuffer, samplesToRead, sourceRate);
        if (includeCorrelation)
            processOfflineCorrelationBlock(snapshot, readBuffer, samplesToRead, sourceRate);
        if (includeLevel)
            processOfflineLevelBlock(snapshot, readBuffer, samplesToRead, sourceRate, levelOptions);
        onProgress(static_cast<float>(readPosition + samplesToRead - sourceStart)
                   / static_cast<float>(sourceEnd - sourceStart));

        if (! includeScope)
            continue;

        const auto* left = readBuffer.getReadPointer(0);
        const auto* right = readBuffer.getReadPointer(1);
        for (int sampleIndex = 0; sampleIndex < samplesToRead; ++sampleIndex)
        {
            const auto sourceOffset = static_cast<double>(
                readPosition + sampleIndex - requestedSourceStart);
            const auto playbackTime = choice.playbackStartSeconds
                + sourceOffset / sourceSamplesPerPlaybackSecond;
            const auto normalizedTime = snapshot.durationSeconds > 0.0
                ? (playbackTime - snapshot.startTimeSeconds) / snapshot.durationSeconds
                : 0.0;
            const auto column = std::min(columnCount - 1,
                static_cast<size_t>(std::max(0.0, normalizedTime)
                                    * static_cast<double>(columnCount)));
            const std::array<float, OfflineAnalysisSnapshot::numChannelModes> widebandModes {
                left[sampleIndex], right[sampleIndex],
                0.5f * (left[sampleIndex] + right[sampleIndex]),
                0.5f * (left[sampleIndex] - right[sampleIndex])
            };

            for (size_t modeIndex = 0; modeIndex < widebandModes.size(); ++modeIndex)
            {
                auto& envelope = snapshot.wideband[modeIndex];
                envelope.minimums[column] = std::min(envelope.minimums[column], widebandModes[modeIndex]);
                envelope.maximums[column] = std::max(envelope.maximums[column], widebandModes[modeIndex]);
            }
            const auto ranges = crossover.processSample(left[sampleIndex], right[sampleIndex]);

            for (size_t bandIndex = 0; bandIndex < snapshot.activeBandCount; ++bandIndex)
            {
                const auto bandLeft = static_cast<float>(ranges[bandIndex].left);
                const auto bandRight = static_cast<float>(ranges[bandIndex].right);
                const std::array<float, OfflineAnalysisSnapshot::numChannelModes> modes {
                    bandLeft,
                    bandRight,
                    0.5f * (bandLeft + bandRight),
                    0.5f * (bandLeft - bandRight)
                };

                for (size_t modeIndex = 0; modeIndex < modes.size(); ++modeIndex)
                {
                    auto& envelope = snapshot.bands[bandIndex][modeIndex];
                    envelope.minimums[column] = std::min(envelope.minimums[column], modes[modeIndex]);
                    envelope.maximums[column] = std::max(envelope.maximums[column], modes[modeIndex]);
                }
            }
        }
    }

    return true;
}

bool analyseHostTakeChoices(
    OfflineAnalysisSnapshot& snapshot,
    ARA::PlugIn::DocumentController* documentController,
    const std::vector<OfflineSourceTakeChoice>& choices,
    const juce::String& sourceId,
    const juce::String& takeSelection,
    const dsp::Crossover::SplitFrequencies& frequencies,
    const size_t activeSplitCount,
    const size_t columnCount,
    const bool includeScope,
    const bool includeFrequency,
    const bool includeCorrelation,
    const bool includeLevel,
    const lvls::MeterProcessor::ProcessingOptions levelOptions,
    const std::function<bool()>& shouldCancel,
    const std::function<void(float)>& onProgress)
{
    const auto selectedTakeNumber = takeSelection.getIntValue();
    std::vector<const OfflineSourceTakeChoice*> selectedChoices;
    for (const auto& choice : choices)
    {
        if (! choice.hostEnumerated
            || (sourceId.isNotEmpty() && choice.sourceId != sourceId)
            || (selectedTakeNumber > 0 && choice.takeNumber != selectedTakeNumber))
            continue;

        selectedChoices.push_back(&choice);
    }

    if (selectedChoices.empty())
    {
        snapshot.startTimeSeconds = 0.0;
        snapshot.durationSeconds = 0.0;
        initialiseOfflineEnvelopes(snapshot, columnCount);
        finishOfflineEnvelopes(snapshot);
        return true;
    }

    auto firstTime = std::numeric_limits<double>::max();
    auto lastTime = std::numeric_limits<double>::lowest();
    auto totalDuration = 0.0;
    for (const auto* choice : selectedChoices)
        totalDuration += std::max(0.0, choice->playbackDurationSeconds);
    auto completedDuration = 0.0;
    for (const auto* choice : selectedChoices)
    {
        firstTime = std::min(firstTime, choice->playbackStartSeconds);
        lastTime = std::max(lastTime,
                            choice->playbackStartSeconds + choice->playbackDurationSeconds);
    }

    snapshot.startTimeSeconds = firstTime;
    snapshot.durationSeconds = std::max(0.0, lastTime - firstTime);
    initialiseOfflineEnvelopes(snapshot, columnCount);

    for (const auto* choice : selectedChoices)
    {
        if (shouldCancel())
            return false;

        auto* audioSource = findHostTakeAudioSource(documentController, *choice);
        if (audioSource == nullptr)
            continue;

        const auto choiceDuration = std::max(0.0, choice->playbackDurationSeconds);
        if (! analyseHostTakeChoice(snapshot, *audioSource, *choice, frequencies,
                                    activeSplitCount, includeScope, includeFrequency,
                                    includeCorrelation, includeLevel, levelOptions, shouldCancel,
                                    [&] (const float localProgress)
                                    {
                                        onProgress(static_cast<float>((completedDuration
                                            + choiceDuration * localProgress)
                                            / std::max(0.001, totalDuration)));
                                    }))
            return false;
        completedDuration += choiceDuration;
        onProgress(static_cast<float>(completedDuration / std::max(0.001, totalDuration)));
    }

    if (includeScope)
        finishOfflineEnvelopes(snapshot);
    return true;
}

class PlaybackRenderer::SharedReaderThread final : public juce::TimeSliceThread
{
public:
    SharedReaderThread()
        : juce::TimeSliceThread("ANA ARA Reader")
    {
        startThread(juce::Thread::Priority::high);
    }
};

class PlaybackRenderer::AudioSourceReader
{
public:
    AudioSourceReader(juce::ARAAudioSource* source,
                      const bool shouldBuffer,
                      juce::TimeSliceThread& readerThread,
                      const int readAheadSize)
    {
        directReader = std::make_unique<juce::ARAAudioSourceReader>(source);

        if (shouldBuffer)
        {
            auto bufferingReader = std::make_unique<juce::BufferingAudioReader>(
                new juce::ARAAudioSourceReader(source), readerThread, readAheadSize);
            bufferingReader->setReadTimeout(0);
            bufferedReader = std::move(bufferingReader);
        }
    }

    bool read(juce::AudioBuffer<float>* const destination,
              const int destinationStartSample,
              const int numberOfSamples,
              const juce::int64 readerStartSample,
              const bool useLeftChannel,
              const bool useRightChannel)
    {
        if (bufferedReader != nullptr
            && bufferedReader->read(destination, destinationStartSample, numberOfSamples,
                                    readerStartSample, useLeftChannel, useRightChannel))
            return true;

        // The cache request above also schedules read-ahead. Until it is ready,
        // use an independent reader so the first audible block is never silence.
        return directReader != nullptr
            && directReader->read(destination, destinationStartSample, numberOfSamples,
                                  readerStartSample, useLeftChannel, useRightChannel);
    }

private:
    std::unique_ptr<juce::AudioFormatReader> directReader;
    std::unique_ptr<juce::AudioFormatReader> bufferedReader;
};

PlaybackRenderer::PlaybackRenderer(ARA::PlugIn::DocumentController* documentController,
                                   ProcessingLock& processingLock)
    : juce::ARAPlaybackRenderer(documentController),
      juce::Thread("ANA ARA Offline Analysis"),
      lock(processingLock),
      araDocumentController(documentController)
{
    startThread(juce::Thread::Priority::high);
}

PlaybackRenderer::~PlaybackRenderer()
{
    signalThreadShouldExit();
    notify();
    stopThread(5000);
}

void PlaybackRenderer::prepareToPlay(const double sampleRate,
                                     const int maximumSamplesPerBlock,
                                     const int numChannels,
                                     juce::AudioProcessor::ProcessingPrecision,
                                     const AlwaysNonRealtime alwaysNonRealtime)
{
    BandlimitedResampler::prepare();
    preparedSampleRate = sampleRate;
    preparedBlockSize = maximumSamplesPerBlock;
    preparedChannelCount = numChannels;
    useBufferedReaders = alwaysNonRealtime == AlwaysNonRealtime::no;
    renderedBuffer = std::make_unique<juce::AudioBuffer<float>>(preparedChannelCount,
                                                                preparedBlockSize);
    mixBuffer = std::make_unique<juce::AudioBuffer<float>>(preparedChannelCount, preparedBlockSize);
    isPrepared = true;
    rebuildReaders();
    scheduleLatestAnalysis();
}

void PlaybackRenderer::rebuildReaders()
{
    readers.clear();
    sourceBufferSize = preparedBlockSize + BandlimitedResampler::kernelRadius * 2 + 4;

    const auto addReader = [this] (juce::ARAAudioSource* audioSource)
    {
        if (audioSource == nullptr || readers.find(audioSource) != readers.end())
            return;

        const auto readAheadSize = std::max(4 * preparedBlockSize,
                                            juce::roundToInt(2.0 * preparedSampleRate));
        readers.emplace(audioSource,
                        std::make_unique<AudioSourceReader>(audioSource,
                                                           useBufferedReaders,
                                                           *sharedReaderThread,
                                                           readAheadSize));
    };

    if (araDocumentController != nullptr)
        if (auto* document = araDocumentController->getDocument())
            for (auto* audioSource : document->getAudioSources<juce::ARAAudioSource>())
                addReader(audioSource);

    for (auto* playbackRegion : getPlaybackRegions())
    {
        auto* audioSource = playbackRegion->getAudioModification()->getAudioSource();
        addReader(audioSource);

        const auto playbackDuration = playbackRegion->getDurationInPlaybackTime();
        const auto modificationDuration = playbackRegion->getDurationInAudioModificationTime();

        if (playbackDuration > 0.0 && preparedSampleRate > 0.0)
        {
            const auto sourceIncrement = audioSource->getSampleRate() / preparedSampleRate
                * modificationDuration / playbackDuration;
            sourceBufferSize = std::max(sourceBufferSize,
                juce::roundToInt(std::ceil(static_cast<double>(preparedBlockSize)
                                           * std::max(1.0, sourceIncrement)))
                    + BandlimitedResampler::kernelRadius * 2 + 4);
        }
    }

    sourceBuffer = std::make_unique<juce::AudioBuffer<float>>(preparedChannelCount, sourceBufferSize);
}

bool PlaybackRenderer::setRealtimeTakeChoices(const std::vector<OfflineSourceTakeChoice>& choices)
{
    auto activeTakes = std::make_shared<std::vector<RealtimeTake>>();
    activeTakes->reserve(choices.size());
    size_t activeChoiceCount = 0;
    for (const auto& choice : choices)
    {
        if (! choice.activeTake || choice.audioSourcePersistentId.isEmpty()
            || choice.playbackDurationSeconds <= 0.0 || choice.playRate <= 0.0)
            continue;

        ++activeChoiceCount;
        auto* audioSource = findHostTakeAudioSource(araDocumentController, choice);
        if (audioSource == nullptr)
            continue;

        activeTakes->push_back({ audioSource,
                                choice.playbackStartSeconds,
                                choice.playbackDurationSeconds,
                                choice.sourceStartSeconds,
                                choice.playRate,
                                choice.gain });
    }
    const auto resolved = activeTakes->size() == activeChoiceCount;
    if (! resolved)
        activeTakes->clear();

    hostTakeSelectionPresent.store(resolved && activeChoiceCount > 0,
                                   std::memory_order_release);
    std::shared_ptr<const std::vector<RealtimeTake>> publishedTakes = std::move(activeTakes);
    std::atomic_store_explicit(&realtimeTakes, std::move(publishedTakes), std::memory_order_release);
    return resolved;
}

void PlaybackRenderer::releaseResources()
{
    isPrepared = false;
    readers.clear();
    renderedBuffer.reset();
    mixBuffer.reset();
    sourceBuffer.reset();
}

bool PlaybackRenderer::processBlock(juce::AudioBuffer<float>& buffer,
                                    const juce::AudioProcessor::Realtime,
                                    const juce::AudioPlayHead::PositionInfo& positionInfo) noexcept
{
    const auto processingLock = lock.getProcessingLock();

    if (! processingLock.isLocked())
        return false;

    const auto numSamples = buffer.getNumSamples();
    const auto timeInSamples = positionInfo.getTimeInSamples().orFallback(0);
    auto success = true;

    jassert(numSamples <= preparedBlockSize);
    jassert(buffer.getNumChannels() == preparedChannelCount);

    if (numSamples > preparedBlockSize
        || buffer.getNumChannels() != preparedChannelCount
        || renderedBuffer == nullptr
        || mixBuffer == nullptr
        || sourceBuffer == nullptr)
        return false;

    renderedBuffer->clear();

    if (positionInfo.getIsPlaying())
    {
        const auto blockRange = juce::Range<juce::int64>::withStartAndLength(timeInSamples, numSamples);
        const auto renderSource = [&] (juce::ARAAudioSource* audioSource,
                                       const juce::Range<juce::int64> renderRange,
                                       const double sourceStart,
                                       const double sourceIncrement,
                                       const float gain)
        {
            const auto reader = readers.find(audioSource);
            if (reader == readers.end())
                return false;

            const auto samplesToRead = static_cast<int>(renderRange.getLength());
            const auto startInBuffer = static_cast<int>(renderRange.getStart() - blockRange.getStart());
            mixBuffer->clear();

            if (std::abs(sourceIncrement - 1.0) < 1.0e-9
                && std::abs(sourceStart - std::round(sourceStart)) < 1.0e-6)
            {
                if (! reader->second->read(mixBuffer.get(),
                                           startInBuffer,
                                           samplesToRead,
                                           static_cast<juce::int64>(std::llround(sourceStart)),
                                           true,
                                           true))
                    return false;
            }
            else
            {
                const auto firstSourceSample = static_cast<juce::int64>(std::floor(sourceStart))
                    - BandlimitedResampler::kernelRadius + 1;
                const auto lastSourcePosition = sourceStart
                    + static_cast<double>(std::max(0, samplesToRead - 1)) * sourceIncrement;
                const auto sourceReadEnd = static_cast<juce::int64>(std::floor(lastSourcePosition))
                    + BandlimitedResampler::kernelRadius + 1;
                const auto sourceReadCount = static_cast<int>(sourceReadEnd - firstSourceSample);

                if (sourceReadCount <= 0 || sourceReadCount > sourceBufferSize)
                    return false;

                sourceBuffer->clear();
                const auto readableStart = std::max<juce::int64>(0, firstSourceSample);
                const auto readableEnd = std::min<juce::int64>(audioSource->getSampleCount(),
                                                                sourceReadEnd);
                const auto destinationOffset = static_cast<int>(readableStart - firstSourceSample);
                const auto readableCount = static_cast<int>(std::max<juce::int64>(
                    0, readableEnd - readableStart));
                if (readableCount > 0
                    && ! reader->second->read(sourceBuffer.get(),
                                              destinationOffset,
                                              readableCount,
                                              readableStart,
                                              true,
                                              true))
                    return false;

                for (int sampleIndex = 0; sampleIndex < samplesToRead; ++sampleIndex)
                {
                    const auto sourcePosition = sourceStart
                        - static_cast<double>(firstSourceSample)
                        + static_cast<double>(sampleIndex) * sourceIncrement;

                    for (int channel = 0; channel < preparedChannelCount; ++channel)
                        mixBuffer->setSample(channel,
                                             startInBuffer + sampleIndex,
                                             BandlimitedResampler::interpolate(
                                                 sourceBuffer->getReadPointer(channel),
                                                 sourceReadCount,
                                                 sourcePosition,
                                                 sourceIncrement));
                }
            }

            for (int channel = 0; channel < preparedChannelCount; ++channel)
                renderedBuffer->addFrom(channel,
                                        startInBuffer,
                                        *mixBuffer,
                                        channel,
                                        startInBuffer,
                                        samplesToRead,
                                        gain);
            return true;
        };

        const auto activeTakes = std::atomic_load_explicit(&realtimeTakes, std::memory_order_acquire);
        const auto hasHostTakeMap = activeTakes != nullptr && ! activeTakes->empty();
        const auto hasHostTakeSelection = hostTakeSelectionPresent.load(std::memory_order_acquire);
        if (hasHostTakeMap)
        {
            for (const auto& take : *activeTakes)
            {
                const auto playbackStart = static_cast<juce::int64>(std::llround(
                    take.playbackStartSeconds * preparedSampleRate));
                const auto playbackLength = static_cast<juce::int64>(std::llround(
                    take.playbackDurationSeconds * preparedSampleRate));
                const auto renderRange = blockRange.getIntersectionWith(
                    juce::Range<juce::int64>::withStartAndLength(playbackStart, playbackLength));
                if (renderRange.isEmpty())
                    continue;

                if (readers.find(take.audioSource) == readers.end())
                {
                    success = false;
                    continue;
                }

                auto* audioSource = take.audioSource;
                const auto sourceRate = audioSource->getSampleRate();
                const auto renderStartTime = static_cast<double>(renderRange.getStart()) / preparedSampleRate;
                const auto sourceStart = (take.sourceStartSeconds
                    + (renderStartTime - take.playbackStartSeconds) * take.playRate) * sourceRate;
                const auto sourceIncrement = sourceRate / preparedSampleRate * take.playRate;
                if (! renderSource(audioSource, renderRange, sourceStart, sourceIncrement,
                                   static_cast<float>(take.gain)))
                    success = false;
            }
        }

        if (! hasHostTakeMap && ! hasHostTakeSelection)
        {
            for (auto* playbackRegion : getPlaybackRegions())
            {
                const auto playbackRange = playbackRegion->getSampleRange(
                    preparedSampleRate, juce::ARAPlaybackRegion::IncludeHeadAndTail::no);
                const auto renderRange = blockRange.getIntersectionWith(playbackRange);
                if (renderRange.isEmpty())
                    continue;

                auto* audioSource = playbackRegion->getAudioModification()->getAudioSource();
                const auto playbackDuration = playbackRegion->getDurationInPlaybackTime();
                if (audioSource == nullptr || playbackDuration <= 0.0)
                {
                    success = false;
                    continue;
                }

                const auto modificationDuration = playbackRegion->getDurationInAudioModificationTime();
                const auto sourceRate = audioSource->getSampleRate();
                const auto sourceIncrement = sourceRate / preparedSampleRate
                    * modificationDuration / playbackDuration;
                const auto renderStartTime = static_cast<double>(renderRange.getStart()) / preparedSampleRate;
                const auto sourceStart = (playbackRegion->getStartInAudioModificationTime()
                    + (renderStartTime - playbackRegion->getStartInPlaybackTime())
                        * modificationDuration / playbackDuration) * sourceRate;
                if (! renderSource(audioSource, renderRange, sourceStart, sourceIncrement, 1.0f))
                    success = false;
            }
        }
    }

    if (success)
        for (int channel = 0; channel < preparedChannelCount; ++channel)
            buffer.copyFrom(channel, 0, *renderedBuffer, channel, 0, numSamples);

    return success;
}

void PlaybackRenderer::didAddPlaybackRegion(ARA::PlugIn::PlaybackRegion*) noexcept
{
    regionGeneration.fetch_add(1, std::memory_order_relaxed);

    if (isPrepared)
    {
        try
        {
            rebuildReaders();
        }
        catch (...)
        {
            readers.clear();
        }
    }

    scheduleLatestAnalysis();
}

void PlaybackRenderer::didRemovePlaybackRegion(ARA::PlugIn::PlaybackRegion*) noexcept
{
    regionGeneration.fetch_add(1, std::memory_order_relaxed);

    if (isPrepared)
    {
        try
        {
            rebuildReaders();
        }
        catch (...)
        {
            readers.clear();
        }
    }

    scheduleLatestAnalysis();
}

void PlaybackRenderer::requestOfflineAnalysis(OfflineAnalysisRequest request,
                                              const bool forceRefresh)
{
    request.regionGeneration = regionGeneration.load(std::memory_order_relaxed);
    const juce::ScopedLock scopedLock(analysisLock);

    if (! forceRefresh
        && latestAnalysisSettings.revision != 0
        && latestAnalysisSettings.hasSameSettings(request))
        return;

    request.revision = latestAnalysisRevision.fetch_add(1, std::memory_order_relaxed) + 1;
    latestAnalysisSettings = std::move(request);
    pendingAnalysis = latestAnalysisSettings;
    analysisProgress.store(0, std::memory_order_release);

    notify();
}

std::shared_ptr<const OfflineAnalysisSnapshot> PlaybackRenderer::getOfflineSnapshot() const
{
    const juce::ScopedLock scopedLock(analysisLock);
    return offlineSnapshot;
}

std::vector<OfflineSourceTakeChoice> PlaybackRenderer::getOfflineSourceTakeChoices() const
{
    const auto processingLock = lock.getProcessingLock();

    if (! processingLock.isLocked())
    {
        const juce::ScopedLock scopedLock(analysisLock);
        return cachedOfflineSourceTakeChoices;
    }

    std::vector<juce::ARAPlaybackRegion*> rendererRegions;

    for (auto* playbackRegion : getPlaybackRegions())
        rendererRegions.push_back(playbackRegion);

    const auto playbackRegions = collectOfflinePlaybackRegions(araDocumentController, rendererRegions);
    auto choices = makeOfflineSourceTakeChoices(araDocumentController, playbackRegions);

    {
        const juce::ScopedLock scopedLock(analysisLock);
        cachedOfflineSourceTakeChoices = choices;
    }

    return choices;
}

void PlaybackRenderer::scheduleLatestAnalysis()
{
    const juce::ScopedLock scopedLock(analysisLock);

    if (latestAnalysisSettings.revision == 0)
        return;

    latestAnalysisSettings.regionGeneration = regionGeneration.load(std::memory_order_relaxed);
    latestAnalysisSettings.revision = latestAnalysisRevision.fetch_add(1, std::memory_order_relaxed) + 1;
    pendingAnalysis = latestAnalysisSettings;
    analysisProgress.store(0, std::memory_order_release);
    notify();
}

void PlaybackRenderer::run()
{
    while (! threadShouldExit())
    {
        wait(-1);

        if (threadShouldExit())
            break;

        std::optional<OfflineAnalysisRequest> request;

        {
            const juce::ScopedLock scopedLock(analysisLock);
            request = pendingAnalysis;
            pendingAnalysis.reset();
        }

        if (! request.has_value())
            continue;

        auto snapshot = buildOfflineSnapshot(*request);

        if (snapshot != nullptr
            && request->revision == latestAnalysisRevision.load(std::memory_order_relaxed))
        {
            const juce::ScopedLock scopedLock(analysisLock);
            offlineSnapshot = std::move(snapshot);
            analysisProgress.store(100, std::memory_order_release);
        }
        else if (snapshot == nullptr
                 && request->revision == latestAnalysisRevision.load(std::memory_order_relaxed)
                 && ! threadShouldExit())
        {
            wait(20);
            const juce::ScopedLock scopedLock(analysisLock);
            pendingAnalysis = request;
            notify();
        }
    }
}

std::shared_ptr<OfflineAnalysisSnapshot> analyseOfflinePlaybackRegions(
    ARA::PlugIn::DocumentController* documentController,
    const std::vector<juce::ARAPlaybackRegion*>& playbackRegions,
    const OfflineAnalysisRequest& request,
    const uint64_t snapshotRevision,
    const std::function<bool()>& shouldCancel,
    const std::function<void(float)>& onProgress)
{
    const auto columnCount = std::max<size_t>(1, request.columnCount);
    constexpr int readBlockSize = 4096;
    const auto sourceTakeChoices = request.sourceTakeChoices.empty()
        ? makeOfflineSourceTakeChoices(documentController, playbackRegions)
        : request.sourceTakeChoices;

    auto snapshot = std::make_shared<OfflineAnalysisSnapshot>();
    const auto includeScope = request.analyzerPage == 0;
    const auto includeFrequency = request.analyzerPage == 1;
    const auto includeCorrelation = request.analyzerPage == 2;
    const auto includeLevel = request.analyzerPage == 3;
    snapshot->activeBandCount = request.activeSplitCount + 1;
    snapshot->revision = snapshotRevision;
    if (includeFrequency)
        prepareOfflineFrequencySpectrum(*snapshot, request.frequencyBlockSize, request.frequencyOverlap,
                                        request.frequencyAveragingTimeMilliseconds);
    if (includeCorrelation)
        prepareOfflineCorrelationSpectrum(*snapshot, request.correlationBlockSize, request.correlationOverlap,
                                          request.correlationAveragingTimeMilliseconds);
    if (includeLevel)
        prepareOfflineLevelMeter(*snapshot);

    if (std::any_of(sourceTakeChoices.begin(), sourceTakeChoices.end(),
                    [] (const auto& choice) { return choice.hostEnumerated; }))
    {
        if (! analyseHostTakeChoices(
                *snapshot, documentController, sourceTakeChoices,
                request.sourceId, request.takeId, request.frequencies,
                request.activeSplitCount, columnCount, includeScope, includeFrequency,
                includeCorrelation, includeLevel, request.levelOptions,
                shouldCancel, onProgress))
            return {};

        return snapshot;
    }

    auto firstTime = std::numeric_limits<double>::max();
    auto lastTime = std::numeric_limits<double>::lowest();
    const auto shouldAnalyse = [&request, &sourceTakeChoices] (
                                   const juce::ARAPlaybackRegion* playbackRegion)
    {
        return playbackRegion != nullptr
            && matchesOfflineSelection(*playbackRegion, sourceTakeChoices,
                                       request.sourceId, request.takeId);
    };

    for (auto* playbackRegion : playbackRegions)
    {
        if (! shouldAnalyse(playbackRegion))
            continue;

        firstTime = std::min(firstTime, playbackRegion->getStartInPlaybackTime());
        lastTime = std::max(lastTime, playbackRegion->getEndInPlaybackTime());
    }

    juce::ARAAudioModification* directTake = nullptr;
    if (firstTime > lastTime)
    {
        directTake = findOfflineTakeModification(
            documentController, sourceTakeChoices, request.sourceId, request.takeId);
        if (directTake == nullptr || directTake->getAudioSource() == nullptr)
            return snapshot;

        firstTime = 0.0;
        lastTime = directTake->getAudioSource()->getDuration();
    }

    snapshot->startTimeSeconds = firstTime;
    snapshot->durationSeconds = std::max(0.0, lastTime - firstTime);

    if (includeScope)
        initialiseOfflineEnvelopes(*snapshot, columnCount);

    for (auto* playbackRegion : playbackRegions)
    {
        if (! shouldAnalyse(playbackRegion))
            continue;

        if (shouldCancel())
            return {};

        auto* audioSource = playbackRegion->getAudioModification()->getAudioSource();
        const auto sourceRate = audioSource->getSampleRate();
        const auto sourceStart = std::max<juce::int64>(0, playbackRegion->getStartInAudioModificationSamples());
        const auto sourceEnd = std::min<juce::int64>(audioSource->getSampleCount(),
                                                     playbackRegion->getEndInAudioModificationSamples());

        if (sourceRate <= 0.0 || sourceEnd <= sourceStart)
            continue;

        juce::ARAAudioSourceReader reader(audioSource);
        juce::AudioBuffer<float> readBuffer(2, readBlockSize);
        dsp::Crossover crossover;
        crossover.prepare(sourceRate);
        crossover.setActiveSplitCount(request.activeSplitCount);
        crossover.setSplitFrequencies(request.frequencies);
        const auto playbackStart = playbackRegion->getStartInPlaybackTime();
        const auto playbackDuration = playbackRegion->getDurationInPlaybackTime();
        const auto sourceLength = static_cast<double>(sourceEnd - sourceStart);

        for (auto readPosition = sourceStart; readPosition < sourceEnd; readPosition += readBlockSize)
        {
            if (shouldCancel())
                return {};

            const auto samplesToRead = static_cast<int>(std::min<juce::int64>(
                readBlockSize, sourceEnd - readPosition));

            if (! reader.read(&readBuffer, 0, samplesToRead, readPosition, true, true))
                continue;

            if (includeFrequency)
                processOfflineFrequencyBlock(*snapshot, readBuffer, samplesToRead, sourceRate);
            if (includeCorrelation)
                processOfflineCorrelationBlock(*snapshot, readBuffer, samplesToRead, sourceRate);
            if (includeLevel)
                processOfflineLevelBlock(*snapshot, readBuffer, samplesToRead, sourceRate,
                                         request.levelOptions);

            if (! includeScope)
                continue;

            const auto* left = readBuffer.getReadPointer(0);
            const auto* right = readBuffer.getReadPointer(1);

            for (int sampleIndex = 0; sampleIndex < samplesToRead; ++sampleIndex)
            {
                const auto sourceOffset = static_cast<double>(readPosition - sourceStart + sampleIndex);
                const auto playbackTime = playbackStart
                    + (sourceOffset / sourceLength) * playbackDuration;
                const auto normalizedTime = snapshot->durationSeconds > 0.0
                    ? (playbackTime - firstTime) / snapshot->durationSeconds
                    : 0.0;
                const auto column = std::min(columnCount - 1,
                    static_cast<size_t>(std::max(0.0, normalizedTime)
                                        * static_cast<double>(columnCount)));
                const std::array<float, OfflineAnalysisSnapshot::numChannelModes> widebandModes {
                    left[sampleIndex], right[sampleIndex],
                    0.5f * (left[sampleIndex] + right[sampleIndex]),
                    0.5f * (left[sampleIndex] - right[sampleIndex])
                };

                for (size_t modeIndex = 0; modeIndex < widebandModes.size(); ++modeIndex)
                {
                    auto& envelope = snapshot->wideband[modeIndex];
                    envelope.minimums[column] = std::min(envelope.minimums[column], widebandModes[modeIndex]);
                    envelope.maximums[column] = std::max(envelope.maximums[column], widebandModes[modeIndex]);
                }
                const auto ranges = crossover.processSample(left[sampleIndex], right[sampleIndex]);

                for (size_t bandIndex = 0; bandIndex < snapshot->activeBandCount; ++bandIndex)
                {
                    const auto bandLeft = static_cast<float>(ranges[bandIndex].left);
                    const auto bandRight = static_cast<float>(ranges[bandIndex].right);
                    const std::array<float, OfflineAnalysisSnapshot::numChannelModes> modes {
                        bandLeft,
                        bandRight,
                        0.5f * (bandLeft + bandRight),
                        0.5f * (bandLeft - bandRight)
                    };

                    for (size_t modeIndex = 0; modeIndex < modes.size(); ++modeIndex)
                    {
                        auto& envelope = snapshot->bands[bandIndex][modeIndex];
                        envelope.minimums[column] = std::min(envelope.minimums[column], modes[modeIndex]);
                        envelope.maximums[column] = std::max(envelope.maximums[column], modes[modeIndex]);
                    }
                }
            }
        }
    }

    if (directTake != nullptr
        && ! analyseOfflineTakeSource(
            *snapshot, *directTake->getAudioSource(), request.frequencies,
            request.activeSplitCount, includeScope, includeFrequency,
            includeCorrelation, includeLevel, request.levelOptions,
            shouldCancel, onProgress))
        return {};

    if (includeScope)
        finishOfflineEnvelopes(*snapshot);

    return snapshot;
}

std::shared_ptr<OfflineAnalysisSnapshot> PlaybackRenderer::buildOfflineSnapshot(
    const OfflineAnalysisRequest& request)
{
    const juce::ScopedReadLock processingLock(lock.getProcessingReadWriteLock());
    std::vector<juce::ARAPlaybackRegion*> rendererRegions;
    for (auto* playbackRegion : getPlaybackRegions())
        rendererRegions.push_back(playbackRegion);

    const auto playbackRegions = collectOfflinePlaybackRegions(araDocumentController, rendererRegions);
    return analyseOfflinePlaybackRegions(
        araDocumentController, playbackRegions, request, request.revision,
        [this, &request]
        {
            return threadShouldExit()
                || request.revision != latestAnalysisRevision.load(std::memory_order_relaxed);
        },
        [this] (const float progress)
        {
            analysisProgress.store(juce::jlimit(0, 99, juce::roundToInt(progress * 100.0f)),
                                   std::memory_order_release);
        });
}
} // namespace ana::ara

#endif
