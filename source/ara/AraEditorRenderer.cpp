#include "AraEditorRenderer.h"

#if JucePlugin_Enable_ARA

#include <algorithm>
#include <limits>
#include <unordered_set>

namespace ana::ara
{
EditorRenderer::EditorRenderer(ARA::PlugIn::DocumentController* documentController,
                               ProcessingLock& processingLock)
    : juce::ARAEditorRenderer(documentController),
      juce::Thread("ANA ARA Track Offline Analysis"),
      lock(processingLock),
      araDocumentController(documentController)
{
    startThread(juce::Thread::Priority::normal);
}

EditorRenderer::~EditorRenderer()
{
    signalThreadShouldExit();
    notify();
    stopThread(5000);

    for (auto* regionSequence : listenedRegionSequences)
        regionSequence->removeListener(this);
}

void EditorRenderer::didAddPlaybackRegion(ARA::PlugIn::PlaybackRegion*) noexcept
{
    regionsChanged();
}

void EditorRenderer::didRemovePlaybackRegion(ARA::PlugIn::PlaybackRegion*) noexcept
{
    regionsChanged();
}

void EditorRenderer::didAddRegionSequence(ARA::PlugIn::RegionSequence* regionSequence) noexcept
{
    auto* sequence = static_cast<juce::ARARegionSequence*>(regionSequence);
    sequence->addListener(this);
    listenedRegionSequences.insert(sequence);
    regionsChanged();
}

void EditorRenderer::willRemoveRegionSequence(ARA::PlugIn::RegionSequence* regionSequence) noexcept
{
    auto* sequence = static_cast<juce::ARARegionSequence*>(regionSequence);
    sequence->removeListener(this);
    listenedRegionSequences.erase(sequence);
    regionsChanged();
}

void EditorRenderer::didAddPlaybackRegionToRegionSequence(juce::ARARegionSequence*,
                                                           juce::ARAPlaybackRegion*)
{
    regionsChanged();
}

void EditorRenderer::willRemovePlaybackRegionFromRegionSequence(juce::ARARegionSequence*,
                                                                 juce::ARAPlaybackRegion*)
{
    regionsChanged();
}

void EditorRenderer::regionsChanged()
{
    regionGeneration.fetch_add(1, std::memory_order_relaxed);
    scheduleLatestAnalysis();
}

void EditorRenderer::requestOfflineAnalysis(
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

std::shared_ptr<const OfflineScopeSnapshot> EditorRenderer::getOfflineSnapshot() const
{
    const juce::ScopedLock scopedLock(analysisLock);
    return offlineSnapshot;
}

std::vector<OfflineSourceTakeChoice> EditorRenderer::getOfflineSourceTakeChoices() const
{
    const auto processingLock = lock.getProcessingLock();

    if (! processingLock.isLocked())
    {
        const juce::ScopedLock scopedLock(analysisLock);
        return cachedOfflineSourceTakeChoices;
    }

    auto choices = makeOfflineSourceTakeChoices(araDocumentController, collectPlaybackRegions());

    {
        const juce::ScopedLock scopedLock(analysisLock);
        cachedOfflineSourceTakeChoices = choices;
    }

    return choices;
}

void EditorRenderer::scheduleLatestAnalysis()
{
    const juce::ScopedLock scopedLock(analysisLock);

    if (latestAnalysisSettings.revision == 0)
        return;

    latestAnalysisSettings.regionGeneration = regionGeneration.load(std::memory_order_relaxed);
    latestAnalysisSettings.revision = latestAnalysisRevision.fetch_add(1, std::memory_order_relaxed) + 1;
    pendingAnalysis = latestAnalysisSettings;
    notify();
}

void EditorRenderer::run()
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

std::vector<juce::ARAPlaybackRegion*> EditorRenderer::collectPlaybackRegions() const
{
    std::vector<juce::ARAPlaybackRegion*> regions;
    std::unordered_set<juce::ARAPlaybackRegion*> seen;

    const auto append = [&regions, &seen] (juce::ARAPlaybackRegion* playbackRegion)
    {
        if (playbackRegion != nullptr && seen.insert(playbackRegion).second)
            regions.push_back(playbackRegion);
    };

    for (auto* playbackRegion : getPlaybackRegions())
        append(playbackRegion);

    for (auto* regionSequence : getRegionSequences())
        for (auto* playbackRegion : regionSequence->getPlaybackRegions())
            append(playbackRegion);

    return collectOfflinePlaybackRegions(araDocumentController, regions);
}

std::shared_ptr<OfflineScopeSnapshot> EditorRenderer::buildOfflineSnapshot(
    const AnalysisRequest& request)
{
    const auto columnCount = std::max<size_t>(1, request.columnCount);
    constexpr int readBlockSize = 4096;
    const auto processingLock = lock.getProcessingLock();

    if (! processingLock.isLocked())
        return {};

    const auto playbackRegions = collectPlaybackRegions();
    const auto sourceTakeChoices = request.sourceTakeChoices.empty()
        ? makeOfflineSourceTakeChoices(araDocumentController, playbackRegions)
        : request.sourceTakeChoices;
    auto snapshot = std::make_shared<OfflineScopeSnapshot>();
    snapshot->activeBandCount = request.activeSplitCount + 1;
    snapshot->revision = request.revision | (uint64_t { 1 } << 63);

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
