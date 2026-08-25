#include "AraPlaybackRenderer.h"

#if JucePlugin_Enable_ARA

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace ana::ara
{
namespace
{
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
                            0.0, 0.0, 0.0, 1.0, false, playbackRegion != nullptr });
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
    OfflineScopeSnapshot& snapshot,
    juce::ARAAudioSource& source,
    const dsp::Crossover::SplitFrequencies& frequencies,
    const size_t activeSplitCount,
    const std::function<bool()>& shouldCancel)
{
    constexpr int readBlockSize = 4096;
    const auto sourceRate = source.getSampleRate();
    const auto sourceSampleCount = source.getSampleCount();
    const auto columnCount = snapshot.bands.front().front().minimums.size();

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

        const auto* left = readBuffer.getReadPointer(0);
        const auto* right = readBuffer.getReadPointer(1);

        for (int sampleIndex = 0; sampleIndex < samplesToRead; ++sampleIndex)
        {
            const auto normalizedTime = static_cast<double>(readPosition + sampleIndex)
                / static_cast<double>(sourceSampleCount);
            const auto column = std::min(columnCount - 1,
                static_cast<size_t>(normalizedTime * static_cast<double>(columnCount)));
            const auto ranges = crossover.processSample(left[sampleIndex], right[sampleIndex]);

            for (size_t bandIndex = 0; bandIndex < snapshot.activeBandCount; ++bandIndex)
            {
                const auto bandLeft = static_cast<float>(ranges[bandIndex].left);
                const auto bandRight = static_cast<float>(ranges[bandIndex].right);
                const std::array<float, OfflineScopeSnapshot::numChannelModes> modes {
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

static juce::String normalizePersistentId(const juce::String& value)
{
    juce::String normalized;
    for (const auto character : value)
        if (juce::CharacterFunctions::getHexDigitValue(character) >= 0)
            normalized += juce::CharacterFunctions::toLowerCase(character);

    return normalized;
}

static juce::String getAudioSourceFileName(const juce::ARAAudioSource& source)
{
    const char* utf8Name = source.getName();
    if (utf8Name == nullptr || *utf8Name == '\0')
        return {};

    return juce::File(juce::String::fromUTF8(utf8Name)).getFileName();
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

    if (choice.audioSourceName.isNotEmpty())
        for (auto* source : document->getAudioSources<juce::ARAAudioSource>())
            if (source != nullptr && getAudioSourceFileName(*source) == choice.audioSourceName)
                return source;

    return nullptr;
}

static void initialiseOfflineEnvelopes(OfflineScopeSnapshot& snapshot, const size_t columnCount)
{
    for (auto& band : snapshot.bands)
    {
        for (auto& envelope : band)
        {
            envelope.minimums.assign(columnCount, std::numeric_limits<float>::max());
            envelope.maximums.assign(columnCount, std::numeric_limits<float>::lowest());
        }
    }
}

static void finishOfflineEnvelopes(OfflineScopeSnapshot& snapshot)
{
    for (auto& band : snapshot.bands)
    {
        for (auto& envelope : band)
        {
            for (size_t column = 0; column < envelope.minimums.size(); ++column)
            {
                if (envelope.minimums[column] > envelope.maximums[column])
                    envelope.minimums[column] = envelope.maximums[column] = 0.0f;
            }
        }
    }
}

static bool analyseHostTakeChoice(
    OfflineScopeSnapshot& snapshot,
    juce::ARAAudioSource& source,
    const OfflineSourceTakeChoice& choice,
    const dsp::Crossover::SplitFrequencies& frequencies,
    const size_t activeSplitCount,
    const std::function<bool()>& shouldCancel)
{
    constexpr int readBlockSize = 4096;
    const auto sourceRate = source.getSampleRate();
    const auto sourceSampleCount = source.getSampleCount();
    const auto columnCount = snapshot.bands.front().front().minimums.size();
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
            const auto ranges = crossover.processSample(left[sampleIndex], right[sampleIndex]);

            for (size_t bandIndex = 0; bandIndex < snapshot.activeBandCount; ++bandIndex)
            {
                const auto bandLeft = static_cast<float>(ranges[bandIndex].left);
                const auto bandRight = static_cast<float>(ranges[bandIndex].right);
                const std::array<float, OfflineScopeSnapshot::numChannelModes> modes {
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
    OfflineScopeSnapshot& snapshot,
    ARA::PlugIn::DocumentController* documentController,
    const std::vector<OfflineSourceTakeChoice>& choices,
    const juce::String& sourceId,
    const juce::String& takeSelection,
    const dsp::Crossover::SplitFrequencies& frequencies,
    const size_t activeSplitCount,
    const size_t columnCount,
    const std::function<bool()>& shouldCancel)
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

        if (! analyseHostTakeChoice(snapshot, *audioSource, *choice, frequencies,
                                    activeSplitCount, shouldCancel))
            return false;
    }

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
        auto sourceReader = std::make_unique<juce::ARAAudioSourceReader>(source);

        if (shouldBuffer)
        {
            auto bufferingReader = std::make_unique<juce::BufferingAudioReader>(
                sourceReader.release(), readerThread, readAheadSize);
            setTimeout = [bufferingReaderPtr = bufferingReader.get()] (const int milliseconds)
            {
                bufferingReaderPtr->setReadTimeout(milliseconds);
            };
            reader = std::move(bufferingReader);
        }
        else
        {
            reader = std::move(sourceReader);
        }
    }

    void setReadTimeout(const int milliseconds)
    {
        if (setTimeout)
            setTimeout(milliseconds);
    }

    juce::AudioFormatReader* get() const noexcept
    {
        return reader.get();
    }

private:
    std::function<void(int)> setTimeout;
    std::unique_ptr<juce::AudioFormatReader> reader;
};

PlaybackRenderer::PlaybackRenderer(ARA::PlugIn::DocumentController* documentController,
                                   ProcessingLock& processingLock)
    : juce::ARAPlaybackRenderer(documentController),
      juce::Thread("ANA ARA Offline Analysis"),
      lock(processingLock),
      araDocumentController(documentController)
{
    startThread(juce::Thread::Priority::normal);
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
    preparedSampleRate = sampleRate;
    preparedBlockSize = maximumSamplesPerBlock;
    preparedChannelCount = numChannels;
    useBufferedReaders = alwaysNonRealtime == AlwaysNonRealtime::no;
    mixBuffer = std::make_unique<juce::AudioBuffer<float>>(preparedChannelCount, preparedBlockSize);
    isPrepared = true;
    rebuildReaders();
    scheduleLatestAnalysis();
}

void PlaybackRenderer::rebuildReaders()
{
    readers.clear();
    sourceBufferSize = preparedBlockSize + 4;

    for (auto* playbackRegion : getPlaybackRegions())
    {
        auto* audioSource = playbackRegion->getAudioModification()->getAudioSource();

        if (readers.find(audioSource) == readers.end())
        {
            const auto readAheadSize = std::max(4 * preparedBlockSize,
                                                juce::roundToInt(2.0 * preparedSampleRate));
            readers.emplace(audioSource,
                            std::make_unique<AudioSourceReader>(audioSource,
                                                               useBufferedReaders,
                                                               *sharedReaderThread,
                                                               readAheadSize));
        }

        const auto playbackDuration = playbackRegion->getDurationInPlaybackTime();
        const auto modificationDuration = playbackRegion->getDurationInAudioModificationTime();

        if (playbackDuration > 0.0 && preparedSampleRate > 0.0)
        {
            const auto sourceIncrement = audioSource->getSampleRate() / preparedSampleRate
                * modificationDuration / playbackDuration;
            sourceBufferSize = std::max(sourceBufferSize,
                juce::roundToInt(std::ceil(static_cast<double>(preparedBlockSize)
                                           * std::max(1.0, sourceIncrement))) + 4);
        }
    }

    sourceBuffer = std::make_unique<juce::AudioBuffer<float>>(preparedChannelCount, sourceBufferSize);
}

void PlaybackRenderer::releaseResources()
{
    isPrepared = false;
    readers.clear();
    mixBuffer.reset();
    sourceBuffer.reset();
}

bool PlaybackRenderer::processBlock(juce::AudioBuffer<float>& buffer,
                                    const juce::AudioProcessor::Realtime realtime,
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
        || mixBuffer == nullptr
        || sourceBuffer == nullptr)
        return false;

    buffer.clear();

    if (positionInfo.getIsPlaying())
    {
        const auto blockRange = juce::Range<juce::int64>::withStartAndLength(timeInSamples, numSamples);

        for (auto* playbackRegion : getPlaybackRegions())
        {
            const auto playbackRange = playbackRegion->getSampleRange(
                preparedSampleRate, juce::ARAPlaybackRegion::IncludeHeadAndTail::no);
            auto renderRange = blockRange.getIntersectionWith(playbackRange);

            if (renderRange.isEmpty())
                continue;

            auto* audioSource = playbackRegion->getAudioModification()->getAudioSource();
            const auto reader = readers.find(audioSource);

            if (reader == readers.end())
            {
                success = false;
                continue;
            }

            reader->second->setReadTimeout(realtime == juce::AudioProcessor::Realtime::no ? 100 : 0);

            const auto samplesToRead = static_cast<int>(renderRange.getLength());
            const auto startInBuffer = static_cast<int>(renderRange.getStart() - blockRange.getStart());
            const auto playbackDuration = playbackRegion->getDurationInPlaybackTime();

            if (playbackDuration <= 0.0)
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
                                            * modificationDuration / playbackDuration)
                * sourceRate;

            mixBuffer->clear();

            if (std::abs(sourceIncrement - 1.0) < 1.0e-9
                && std::abs(sourceStart - std::round(sourceStart)) < 1.0e-6)
            {
                if (! reader->second->get()->read(mixBuffer.get(),
                                                  startInBuffer,
                                                  samplesToRead,
                                                  static_cast<juce::int64>(std::llround(sourceStart)),
                                                  true,
                                                  true))
                {
                    success = false;
                    continue;
                }
            }
            else
            {
                const auto sourceReadStart = static_cast<juce::int64>(std::floor(sourceStart));
                const auto sourceReadCount = juce::roundToInt(std::ceil(
                    sourceStart - static_cast<double>(sourceReadStart)
                    + static_cast<double>(std::max(0, samplesToRead - 1)) * sourceIncrement)) + 2;

                if (sourceReadCount > sourceBufferSize
                    || sourceReadStart < 0
                    || ! reader->second->get()->read(sourceBuffer.get(),
                                                     0,
                                                     sourceReadCount,
                                                     sourceReadStart,
                                                     true,
                                                     true))
                {
                    success = false;
                    continue;
                }

                for (int sampleIndex = 0; sampleIndex < samplesToRead; ++sampleIndex)
                {
                    const auto sourcePosition = sourceStart
                        - static_cast<double>(sourceReadStart)
                        + static_cast<double>(sampleIndex) * sourceIncrement;
                    const auto sourceIndex = static_cast<int>(std::floor(sourcePosition));
                    const auto fraction = static_cast<float>(sourcePosition - static_cast<double>(sourceIndex));

                    for (int channel = 0; channel < preparedChannelCount; ++channel)
                    {
                        const auto first = sourceBuffer->getSample(channel, sourceIndex);
                        const auto second = sourceBuffer->getSample(channel, sourceIndex + 1);
                        mixBuffer->setSample(channel,
                                             startInBuffer + sampleIndex,
                                             first + fraction * (second - first));
                    }
                }
            }

            for (int channel = 0; channel < preparedChannelCount; ++channel)
                buffer.addFrom(channel,
                               startInBuffer,
                               *mixBuffer,
                               channel,
                               startInBuffer,
                               samplesToRead);
        }
    }

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

void PlaybackRenderer::requestOfflineAnalysis(
    const size_t activeSplitCount,
    const dsp::Crossover::SplitFrequencies& frequencies,
    const size_t columnCount,
    const juce::String& sourceId,
    const juce::String& takeId,
    const std::vector<OfflineSourceTakeChoice>& sourceTakeChoices,
    const bool forceRefresh)
{
    const auto currentRegionGeneration = regionGeneration.load(std::memory_order_relaxed);
    const juce::ScopedLock scopedLock(analysisLock);

    if (! forceRefresh
        && latestAnalysisSettings.revision != 0
        && latestAnalysisSettings.activeSplitCount == activeSplitCount
        && latestAnalysisSettings.frequencies == frequencies
        && latestAnalysisSettings.columnCount == columnCount
        && latestAnalysisSettings.sourceId == sourceId
        && latestAnalysisSettings.takeId == takeId
        && latestAnalysisSettings.sourceTakeChoices == sourceTakeChoices
        && latestAnalysisSettings.regionGeneration == currentRegionGeneration)
        return;

    latestAnalysisSettings.activeSplitCount = std::min(activeSplitCount, dsp::Crossover::numSplits);
    latestAnalysisSettings.frequencies = frequencies;
    latestAnalysisSettings.columnCount = std::max<size_t>(1, columnCount);
    latestAnalysisSettings.sourceId = sourceId;
    latestAnalysisSettings.takeId = takeId;
    latestAnalysisSettings.sourceTakeChoices = sourceTakeChoices;
    latestAnalysisSettings.regionGeneration = currentRegionGeneration;
    latestAnalysisSettings.revision = latestAnalysisRevision.fetch_add(1, std::memory_order_relaxed) + 1;
    pendingAnalysis = latestAnalysisSettings;

    if (forceRefresh)
        offlineSnapshot.reset();

    notify();
}

std::shared_ptr<const OfflineScopeSnapshot> PlaybackRenderer::getOfflineSnapshot() const
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
    notify();
}

void PlaybackRenderer::run()
{
    while (! threadShouldExit())
    {
        wait(-1);

        if (threadShouldExit())
            break;

        std::optional<AnalysisRequest> request;

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

std::shared_ptr<OfflineScopeSnapshot> PlaybackRenderer::buildOfflineSnapshot(
    const AnalysisRequest& request)
{
    const auto columnCount = std::max<size_t>(1, request.columnCount);
    constexpr int readBlockSize = 4096;
    const auto processingLock = lock.getProcessingLock();

    if (! processingLock.isLocked())
        return {};

    std::vector<juce::ARAPlaybackRegion*> rendererRegions;
    for (auto* playbackRegion : getPlaybackRegions())
        rendererRegions.push_back(playbackRegion);
    const auto playbackRegions = collectOfflinePlaybackRegions(araDocumentController, rendererRegions);
    const auto sourceTakeChoices = request.sourceTakeChoices.empty()
        ? makeOfflineSourceTakeChoices(araDocumentController, playbackRegions)
        : request.sourceTakeChoices;

    auto snapshot = std::make_shared<OfflineScopeSnapshot>();
    snapshot->activeBandCount = request.activeSplitCount + 1;
    snapshot->revision = request.revision;

    if (std::any_of(sourceTakeChoices.begin(), sourceTakeChoices.end(),
                    [] (const auto& choice) { return choice.hostEnumerated; }))
    {
        if (! analyseHostTakeChoices(
                *snapshot, araDocumentController, sourceTakeChoices,
                request.sourceId, request.takeId, request.frequencies,
                request.activeSplitCount, columnCount,
                [this, &request]
                {
                    return threadShouldExit()
                        || request.revision != latestAnalysisRevision.load(std::memory_order_relaxed);
                }))
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
            araDocumentController, sourceTakeChoices, request.sourceId, request.takeId);
        if (directTake == nullptr || directTake->getAudioSource() == nullptr)
            return snapshot;

        firstTime = 0.0;
        lastTime = directTake->getAudioSource()->getDuration();
    }

    snapshot->startTimeSeconds = firstTime;
    snapshot->durationSeconds = std::max(0.0, lastTime - firstTime);

    for (auto& band : snapshot->bands)
    {
        for (auto& envelope : band)
        {
            envelope.minimums.assign(columnCount, std::numeric_limits<float>::max());
            envelope.maximums.assign(columnCount, std::numeric_limits<float>::lowest());
        }
    }

    for (auto* playbackRegion : playbackRegions)
    {
        if (! shouldAnalyse(playbackRegion))
            continue;

        if (threadShouldExit()
            || request.revision != latestAnalysisRevision.load(std::memory_order_relaxed))
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
            if (threadShouldExit()
                || request.revision != latestAnalysisRevision.load(std::memory_order_relaxed))
                return {};

            const auto samplesToRead = static_cast<int>(std::min<juce::int64>(
                readBlockSize, sourceEnd - readPosition));

            if (! reader.read(&readBuffer, 0, samplesToRead, readPosition, true, true))
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
                const auto ranges = crossover.processSample(left[sampleIndex], right[sampleIndex]);

                for (size_t bandIndex = 0; bandIndex < snapshot->activeBandCount; ++bandIndex)
                {
                    const auto bandLeft = static_cast<float>(ranges[bandIndex].left);
                    const auto bandRight = static_cast<float>(ranges[bandIndex].right);
                    const std::array<float, OfflineScopeSnapshot::numChannelModes> modes {
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
            request.activeSplitCount,
            [this, &request]
            {
                return threadShouldExit()
                    || request.revision != latestAnalysisRevision.load(std::memory_order_relaxed);
            }))
        return {};

    for (auto& band : snapshot->bands)
    {
        for (auto& envelope : band)
        {
            for (size_t column = 0; column < columnCount; ++column)
            {
                if (envelope.minimums[column] > envelope.maximums[column])
                    envelope.minimums[column] = envelope.maximums[column] = 0.0f;
            }
        }
    }

    return snapshot;
}
} // namespace ana::ara

#endif
