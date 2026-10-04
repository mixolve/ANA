#include "Processor.h"
#include "offline/shell/AnalysisRequest.h"
#include "offline/shell/AnalysisResult.h"
#include "EditorRenderer.h"
#include "PlaybackRenderer.h"

#include <algorithm>
#include <limits>


bool PluginProcessor::isOfflineSourceAvailable() const noexcept
{
    if (isBoundToARA() && (isPlaybackRenderer() || isEditorRenderer()))
        return true;
    const auto& bridge = isOfflineMonitorLocation() ? monitorHostBridge : reaperHostBridge;
    return bridge.hasTakeCatalogue();
}

int PluginProcessor::getOfflineAnalysisProgress() const noexcept
{
    if (offlineRefreshPending.load(std::memory_order_acquire))
        return 0;
    if (auto* renderer = getEditorRenderer<ana::offline::EditorRenderer>())
        return renderer->getAnalysisProgress();
    if (auto* renderer = getPlaybackRenderer<ana::offline::PlaybackRenderer>())
        return renderer->getAnalysisProgress();

    return hostAnalysisWorker.getProgress();
}

void PluginProcessor::cancelOfflineAnalysis()
{
    hostAnalysisWorker.cancel();
    if (auto* renderer = getEditorRenderer<ana::offline::EditorRenderer>())
        renderer->cancelAnalysis();
    if (auto* renderer = getPlaybackRenderer<ana::offline::PlaybackRenderer>())
        renderer->cancelAnalysis();
}

void PluginProcessor::requestOfflineAnalysis(const size_t columnCount, const bool forceRefresh,
                                             const bool specMapMode, const size_t specMapRowCount)
{
    if (! isOfflineMode())
        return;
    const auto pendingRefresh = offlineRefreshPending.exchange(false, std::memory_order_acq_rel);
    const auto shouldForceRefresh = forceRefresh || pendingRefresh;
    syncOfflineAnalysisSources();
    const auto crossoverCount = getCrossoverCount();
    const auto frequencies = getCrossoverFrequencies();
    const auto sourceChoices = getSourceChoices();
    refreshOfflineSelection(sourceChoices);
    const auto sourceId = getSelectedOfflineSourceId();
    const auto takeNumber = getSelectedOfflineTakeNumber();
    const auto& fftSizes = ana::fft::StereoFftStream::supportedFftSizes;
    const auto* specFftSizeValue = getRawParameterValue(specFftSizeParameterId);
    const auto specFftSizeIndex = juce::jlimit(0, static_cast<int>(fftSizes.size()) - 1,
                                             specFftSizeValue != nullptr
                                                 ? juce::roundToInt(specFftSizeValue->load(std::memory_order_relaxed))
                                                 : static_cast<int>(ana::fft::StereoFftStream::defaultFftSizeIndex));
    const auto* specFftOverlapValue = getRawParameterValue(specFftOverlapParameterId);
    const auto specFftOverlap = specFftOverlapValue != nullptr ? specFftOverlapValue->load(std::memory_order_relaxed)
                                       : ana::fft::StereoFftStream::defaultOverlap;
    const auto* mapTimeOverlapValue = getRawParameterValue(specMapTimeOverlapParameterId);
    const auto mapTimeOverlapChoice = juce::roundToInt(
        mapTimeOverlapValue != nullptr ? mapTimeOverlapValue->load(std::memory_order_relaxed) : 0.0f);
    const auto specMapTimeOverlapFraction = ana::spec::SpecProcessor::mapTimeOverlapFraction(mapTimeOverlapChoice);
    const auto* corrFftSizeValue = getRawParameterValue(corrFftSizeParameterId);
    const auto corrFftSizeIndex = juce::jlimit(0, static_cast<int>(fftSizes.size()) - 1,
        corrFftSizeValue != nullptr
            ? juce::roundToInt(corrFftSizeValue->load(std::memory_order_relaxed)) : static_cast<int>(ana::fft::StereoFftStream::defaultFftSizeIndex));
    const auto* corrFftOverlapValue = getRawParameterValue(corrFftOverlapParameterId);
    const auto corrFftOverlap = corrFftOverlapValue != nullptr
        ? corrFftOverlapValue->load(std::memory_order_relaxed)
        : ana::fft::StereoFftStream::defaultOverlap;
    const auto analyzerPage = getAnalyzerPageState();
    const auto includeLvlsPeakRms = isParameterEnabled(lvlsPeakRmsVisibleParameterId);
    const auto includeLvlsLoudness = isParameterEnabled(lvlsLoudnessVisibleParameterId);
    const auto includeLvlsHistory = isParameterEnabled(lvlsHistoryVisibleParameterId);
    const auto* lvlsRmsWindow = getRawParameterValue(lvlsRmsWindowMsParameterId);
    const auto* lvlsPeakHold = getRawParameterValue(lvlsPeakHoldMsParameterId);

    ana::offline::AnalysisRequest request;
    request.crossoverCount = std::min(crossoverCount, ana::dsp::LinkwitzRileyCrossover::numCrossovers);
    request.frequencies = frequencies;
    request.columnCount = std::max<size_t>(1, columnCount);
    request.specFftSize = fftSizes[static_cast<size_t>(specFftSizeIndex)];
    request.specMapMode = analyzerPage == ana::AnalyzerPage::spec && specMapMode;
    request.specMapRowCount = std::clamp<size_t>(
        specMapRowCount,
        ana::offline::SpectrogramMap::minimumRowCount,
        ana::offline::SpectrogramMap::maximumRowCount);
    request.specFftOverlap = specFftOverlap;
    request.specMapTimeOverlapFraction = specMapTimeOverlapFraction;
    request.corrFftSize = fftSizes[static_cast<size_t>(corrFftSizeIndex)];
    request.corrFftOverlap = corrFftOverlap;
    if (const auto* mode = getRawParameterValue(corrModeParameterId))
        request.corrMode = juce::jlimit(0, ana::corr::CorrProcessor::modeCount - 1,
                                 juce::roundToInt(mode->load(std::memory_order_relaxed)));
    request.sourceId = sourceId;
    request.takeNumber = takeNumber;
    request.sourceChoices = sourceChoices;
    request.useHostTakeChoices = isOfflineMonitorLocation()
        || reaperHostBridge.hasTakeCatalogue();
    request.analyzerPage = analyzerPage;
    request.includeLvlsPeakRms = includeLvlsPeakRms;
    request.includeLvlsLoudness = includeLvlsLoudness;
    request.includeLvlsHistory = includeLvlsHistory;
    request.lvlsRmsWindowMs = lvlsRmsWindow != nullptr
        ? lvlsRmsWindow->load(std::memory_order_relaxed) : ana::lvls::defaultRmsWindowMilliseconds;
    request.lvlsPeakHoldMs = lvlsPeakHold != nullptr
        ? lvlsPeakHold->load(std::memory_order_relaxed) : ana::lvls::defaultPeakHoldMilliseconds;

    auto* playbackRenderer = getPlaybackRenderer<ana::offline::PlaybackRenderer>();
    auto* editorRenderer = getEditorRenderer<ana::offline::EditorRenderer>();
    if (playbackRenderer == nullptr && editorRenderer == nullptr)
    {
        // Without ARA roles there are no native regions to fall back to. An
        // empty host catalogue must also replace a previous MONITOR result.
        request.useHostTakeChoices = true;
        hostAnalysisWorker.request(std::move(request), shouldForceRefresh);
        return;
    }

    if (auto* renderer = playbackRenderer)
        renderer->requestOfflineAnalysis(request, shouldForceRefresh);

    if (auto* renderer = editorRenderer)
        renderer->requestOfflineAnalysis(std::move(request), shouldForceRefresh);
}

std::shared_ptr<const ana::offline::AnalysisResult> PluginProcessor::getAnalysisResult() const
{
    std::shared_ptr<const ana::offline::AnalysisResult> playbackAnalysis;

    if (auto* renderer = getPlaybackRenderer<ana::offline::PlaybackRenderer>())
        playbackAnalysis = renderer->getAnalysisResult();

    if (auto* renderer = getEditorRenderer<ana::offline::EditorRenderer>())
    {
        auto editorAnalysis = renderer->getAnalysisResult();

        if (editorAnalysis != nullptr
            && (editorAnalysis->durationSeconds > 0.0 || editorAnalysis->selectionEmpty))
            return editorAnalysis;
    }

    if (getPlaybackRenderer() == nullptr && getEditorRenderer() == nullptr)
        return hostAnalysisWorker.getAnalysisResult();
    return playbackAnalysis;
}

juce::Image PluginProcessor::getCachedOfflineSpectrogramImage() const
{
    const juce::ScopedLock scopedLock(offlineSpectrogramImageLock);
    return cachedOfflineSpectrogramImage;
}

void PluginProcessor::setCachedOfflineSpectrogramImage(const juce::Image& image)
{
    if (! image.isValid())
        return;

    const juce::ScopedLock scopedLock(offlineSpectrogramImageLock);
    cachedOfflineSpectrogramImage = image;
}

std::vector<ana::offline::SourceChoice> PluginProcessor::getSourceChoices() const
{
    {
        const juce::ScopedLock scopedLock(offlineSelectionLock);
        if (offlineAnalysisSourcesInitialised)
            return offlineAnalysisSourceChoices;
    }
    if (isOfflineMonitorLocation())
        return monitorHostBridge.getTakeChoices();
    auto reaperChoices = reaperHostBridge.getTakeChoices();
    if (reaperHostBridge.hasTakeCatalogue())
        return reaperChoices;

    if (auto* renderer = getEditorRenderer<ana::offline::EditorRenderer>())
    {
        auto choices = renderer->getSourceChoices();

        if (! choices.empty())
            return choices;
    }

    if (auto* renderer = getPlaybackRenderer<ana::offline::PlaybackRenderer>())
        return renderer->getSourceChoices();

    return {};
}

bool PluginProcessor::isOfflineLocationAvailable() const
{
    return reaperHostBridge.isAvailable();
}

bool PluginProcessor::isOfflineMonitorLocation() const noexcept
{
    return isOfflineLocationAvailable() && offlineMonitorLocation.load(std::memory_order_acquire);
}

int PluginProcessor::getOfflineTrackNumber() const
{
    {
        const juce::ScopedLock scopedLock(offlineSelectionLock);
        if (offlineAnalysisSourcesInitialised)
            return offlineAnalysisTrackNumber;
    }
    return (isOfflineMonitorLocation() ? monitorHostBridge : reaperHostBridge).getTrackNumber();
}

juce::String PluginProcessor::getOfflineTrackName() const
{
    if (! isOfflineMonitorLocation())
        return "CURRENT";
    {
        const juce::ScopedLock scopedLock(offlineSelectionLock);
        if (offlineAnalysisSourcesInitialised)
            return offlineAnalysisTrackName;
    }
    return monitorHostBridge.getTrackName();
}

void PluginProcessor::setOfflineMonitorLocation(const bool requestedMonitor)
{
    const auto monitor = requestedMonitor && isOfflineLocationAvailable();
    if (offlineMonitorLocation.exchange(monitor, std::memory_order_acq_rel) == monitor)
        return;
    {
        const juce::ScopedLock scopedLock(offlineSelectionLock);
        offlineAnalysisSourcesInitialised = false;
    }
    monitorHostBridge.setEnabled(monitor);
    forceOfflineRefresh();
    parameters.state.setProperty(offlineLocationStateKey, monitor ? 1 : 0, nullptr);
    updateHostDisplay(juce::AudioProcessorListener::ChangeDetails()
                          .withNonParameterStateChanged(true));
}

bool PluginProcessor::isOfflineAutomaticRefresh() const noexcept
{
    return offlineAutomaticRefresh.load(std::memory_order_acquire);
}

void PluginProcessor::setOfflineAutomaticRefresh(const bool automatic)
{
    if (offlineAutomaticRefresh.exchange(automatic, std::memory_order_acq_rel) == automatic)
        return;
    parameters.state.setProperty(offlineAutomaticRefreshStateKey, automatic, nullptr);
    if (automatic)
        forceOfflineRefresh();
    updateHostDisplay(juce::AudioProcessorListener::ChangeDetails()
                          .withNonParameterStateChanged(true));
}

void PluginProcessor::syncOfflineAnalysisSources(const bool force)
{
    {
        const juce::ScopedLock scopedLock(offlineSelectionLock);
        if (! force && ! isOfflineAutomaticRefresh() && offlineAnalysisSourcesInitialised)
            return;
    }
    const auto& bridge = isOfflineMonitorLocation() ? monitorHostBridge : reaperHostBridge;
    if (! bridge.hasTakeCatalogue())
        return;
    auto choices = bridge.getTakeChoices();
    const auto trackNumber = bridge.getTrackNumber();
    const auto trackName = bridge.getTrackName();
    {
        const juce::ScopedLock scopedLock(offlineSelectionLock);
        offlineAnalysisSourceChoices = choices;
        offlineAnalysisTrackNumber = trackNumber;
        offlineAnalysisTrackName = trackName;
        offlineAnalysisSourcesInitialised = true;
    }
    refreshOfflineSelection(choices);
}

void PluginProcessor::forceOfflineRefresh()
{
    auto& bridge = isOfflineMonitorLocation() ? monitorHostBridge : reaperHostBridge;
    bridge.refresh();
    syncOfflineAnalysisSources(true);
    // The active view supplies the appropriate MAP raster or SCOP geometry
    // on its next timer tick; force even when source metadata is unchanged.
    offlineRefreshPending.store(true, std::memory_order_release);
}

void PluginProcessor::refreshOfflineSelection(const std::vector<ana::offline::SourceChoice>& choices)
{
    const auto& bridge = isOfflineMonitorLocation() ? monitorHostBridge : reaperHostBridge;
    // Do not mistake a partially attached ARA document for REAPER's complete
    // item/take catalogue while restoring a preset.
    if (choices.empty() || (bridge.isAvailable() && ! bridge.hasTakeCatalogue()))
        return;

    auto sourceId = getSelectedOfflineSourceId();
    if (sourceId.isNotEmpty() && std::none_of(choices.begin(), choices.end(),
        [&sourceId] (const auto& choice) { return choice.sourceId == sourceId; }))
    {
        setSelectedOfflineSourceId({});
        sourceId.clear();
    }
    if (isOfflineKeepSecondTake() && std::any_of(choices.begin(), choices.end(),
        [&sourceId] (const auto& choice)
        {
            return choice.takeNumber == 2
                && (sourceId.isEmpty() || choice.sourceId == sourceId);
        }))
    {
        setSelectedOfflineTakeNumber(2);
        return;
    }
    const auto takeNumber = getSelectedOfflineTakeNumber();
    auto firstTake = std::numeric_limits<int>::max();
    for (const auto& choice : choices)
    {
        if (sourceId.isNotEmpty() && choice.sourceId != sourceId)
            continue;
        if (choice.takeNumber == takeNumber)
            return;
        firstTake = std::min(firstTake, choice.takeNumber);
    }
    if (firstTake != std::numeric_limits<int>::max())
        setSelectedOfflineTakeNumber(firstTake);
}

bool PluginProcessor::isOfflineKeepSecondTake() const noexcept
{
    return offlineKeepSecondTake.load(std::memory_order_acquire);
}

void PluginProcessor::setOfflineKeepSecondTake(const bool keep)
{
    if (offlineKeepSecondTake.exchange(keep, std::memory_order_acq_rel) == keep)
        return;
    parameters.state.setProperty(offlineKeepSecondTakeStateKey, keep, nullptr);
    if (keep)
        refreshOfflineSelection(getSourceChoices());
    updateHostDisplay(juce::AudioProcessorListener::ChangeDetails()
                          .withNonParameterStateChanged(true));
}

juce::String PluginProcessor::getSelectedOfflineSourceId() const
{
    const juce::ScopedLock scopedLock(offlineSelectionLock);
    return selectedOfflineSourceId;
}

int PluginProcessor::getSelectedOfflineTakeNumber() const noexcept
{
    const juce::ScopedLock scopedLock(offlineSelectionLock);
    return selectedOfflineTakeNumber;
}

void PluginProcessor::setSelectedOfflineSourceId(const juce::String& sourceId)
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

void PluginProcessor::setSelectedOfflineTakeNumber(const int takeNumber)
{
    const auto validTakeNumber = juce::jmax(1, takeNumber);
    {
        const juce::ScopedLock scopedLock(offlineSelectionLock);
        if (selectedOfflineTakeNumber == validTakeNumber)
            return;
        selectedOfflineTakeNumber = validTakeNumber;
    }

    parameters.state.setProperty(offlineTakeNumberStateKey, validTakeNumber, nullptr);

    updateHostDisplay(juce::AudioProcessorListener::ChangeDetails()
                          .withNonParameterStateChanged(true));
}
