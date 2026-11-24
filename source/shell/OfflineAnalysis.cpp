#include "Processor.h"
#include "offline/shell/AnalysisRequest.h"
#include "offline/shell/AnalysisResult.h"
#include "EditorRenderer.h"
#include "PlaybackRenderer.h"

#include <algorithm>


bool PluginProcessor::isOfflineSourceAvailable() const noexcept
{
    if (isBoundToARA() && (isPlaybackRenderer() || isEditorRenderer()))
        return true;
    const auto& bridge = isOfflineMonitorLocation() ? monitorHostBridge : reaperHostBridge;
    return bridge.hasTakeCatalogue();
}

bool PluginProcessor::usesHostFileAnalysis() const noexcept
{
    return isOfflineMonitorLocation() || reaperHostBridge.hasTakeCatalogue();
}

int PluginProcessor::getOfflineAnalysisProgress() const noexcept
{
    if (offlineRefreshPending.load(std::memory_order_acquire))
        return 0;
    if (usesHostFileAnalysis())
        return hostAnalysisWorker.getProgress();
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
    if (! isOfflineMode() || ! juce::Process::isForegroundProcess())
        return;
    const auto pendingRefresh = offlineRefreshPending.exchange(false, std::memory_order_acq_rel);
    const auto shouldForceRefresh = forceRefresh || pendingRefresh;
    syncOfflineAnalysisSources();
    const auto crossoverCount = getCrossoverCount();
    const auto frequencies = getCrossoverFrequencies();
    const auto sourceChoices = getSourceChoices();
    refreshOfflineSelection(sourceChoices);
    const juce::String sourceId; // Automatic: all items on the current track, or selected items.
    const auto takeNumber = isOfflineMonitorLocation()
        ? (isOfflineKeepSecondTake() ? 2 : 1)
        : getSelectedOfflineTakeNumber();
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
    const auto includeLvlsMetrics = isParameterEnabled(lvlsMetricsVisibleParameterId);
    const auto* lvlsRmsWindow = getRawParameterValue(lvlsRmsWindowMsParameterId);
    const auto* lvlsPeakHold = getRawParameterValue(lvlsPeakHoldMsParameterId);

    ana::offline::AnalysisRequest request;
    request.crossoverCount = std::min(crossoverCount, ana::dsp::LinkwitzRileyCrossover::numCrossovers);
    request.frequencies = frequencies;
    if (const auto* slope = getRawParameterValue(scopSlopeParameterId))
        request.scopSlopeDbPerOctave = slope->load(std::memory_order_relaxed);
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
    request.useHostTakeChoices = usesHostFileAnalysis();
    request.analyzerPage = analyzerPage;
    request.includeLvlsPeakRms = includeLvlsPeakRms;
    request.includeLvlsLoudness = includeLvlsLoudness;
    request.includeLvlsMetrics = includeLvlsMetrics;
    request.lvlsRmsWindowMs = lvlsRmsWindow != nullptr
        ? lvlsRmsWindow->load(std::memory_order_relaxed) : ana::lvls::defaultRmsWindowMilliseconds;
    request.lvlsPeakHoldMs = lvlsPeakHold != nullptr
        ? lvlsPeakHold->load(std::memory_order_relaxed) : ana::lvls::defaultPeakHoldMilliseconds;

    auto* playbackRenderer = getPlaybackRenderer<ana::offline::PlaybackRenderer>();
    auto* editorRenderer = getEditorRenderer<ana::offline::EditorRenderer>();
    if (request.useHostTakeChoices || (playbackRenderer == nullptr && editorRenderer == nullptr))
    {
        // REAPER's catalogue and source files are independent of ARA focus roles.
        // Keep their worker/result owned by the processor, including when a
        // renderer is removed and reattached without a media edit.
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
    if (usesHostFileAnalysis())
        return hostAnalysisWorker.getAnalysisResult();
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
    // A host-interface reattachment must not flip the UI to realtime while
    // REAPER's source catalogue and processor-owned offline result remain valid.
    return reaperHostBridge.isAvailable() || reaperHostBridge.hasTakeCatalogue()
        || monitorHostBridge.hasTakeCatalogue();
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
    auto* parameter = parameters.getParameter(offlineMonitorParameterId);
    const auto normalized = monitor ? 1.0f : 0.0f;
    if (!juce::approximatelyEqual(parameter->getValue(), normalized)) parameter->setValueNotifyingHost(normalized);
    if (offlineMonitorLocation.exchange(monitor, std::memory_order_acq_rel) == monitor)
        return;
    {
        const juce::ScopedLock scopedLock(offlineSelectionLock);
        offlineAnalysisSourcesInitialised = false;
    }
    monitorHostBridge.setEnabled(monitor);
    forceOfflineRefresh();
}

bool PluginProcessor::shouldAutomaticallyRefreshOfflineSources() const noexcept
{
    // MONITOR follows host selection even if automatic TRACK refresh is disabled.
    return isOfflineMonitorLocation() || offlineAutomaticRefresh.load(std::memory_order_acquire);
}

void PluginProcessor::setOfflineAutomaticRefresh(const bool automatic)
{
    if (offlineAutomaticRefresh.exchange(automatic, std::memory_order_acq_rel) == automatic)
        return;
    parameters.state.setProperty(offlineAutomaticRefreshStateKey, automatic, nullptr);
    if (automatic)
        forceOfflineRefresh();
}

void PluginProcessor::syncOfflineAnalysisSources(const bool force)
{
    {
        const juce::ScopedLock scopedLock(offlineSelectionLock);
        if (! force && ! shouldAutomaticallyRefreshOfflineSources() && offlineAnalysisSourcesInitialised)
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
    if (restoringState.load(std::memory_order_acquire) || choices.empty()) return;
    const auto& bridge = isOfflineMonitorLocation() ? monitorHostBridge : reaperHostBridge;
    if (bridge.isAvailable() && ! bridge.hasTakeCatalogue()) return;

    if (getSelectedOfflineSourceId().isNotEmpty())
        setSelectedOfflineSourceId({});
}

bool PluginProcessor::isOfflineKeepSecondTake() const noexcept
{
    return offlineKeepSecondTake.load(std::memory_order_acquire);
}

void PluginProcessor::setOfflineKeepSecondTake(const bool keep)
{
    auto* parameter = parameters.getParameter(offlineKeepTakeParameterId);
    const auto normalized = keep ? 1.0f : 0.0f;
    if (!juce::approximatelyEqual(parameter->getValue(), normalized)) parameter->setValueNotifyingHost(normalized);
    if (offlineKeepSecondTake.exchange(keep, std::memory_order_acq_rel) == keep)
        return;
    if (isOfflineMonitorLocation())
        forceOfflineRefresh();
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
}

void PluginProcessor::setSelectedOfflineTakeNumber(const int takeNumber)
{
    const auto validTakeNumber = juce::jlimit(1, 2, takeNumber);
    {
        const juce::ScopedLock scopedLock(offlineSelectionLock);
        if (selectedOfflineTakeNumber == validTakeNumber)
            return;
        selectedOfflineTakeNumber = validTakeNumber;
    }

    parameters.state.setProperty(offlineTakeNumberStateKey, validTakeNumber, nullptr);
    if (! isOfflineMonitorLocation())
        forceOfflineRefresh();
}

void PluginProcessor::requestOfflineRefresh()
{
    if (!isOfflineMode()) return;
    auto* parameter = parameters.getParameter(offlineRefreshParameterId);
    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost(1.0f);
    parameter->endChangeGesture();
    // A second explicit request must still execute if the momentary parameter
    // is already high while its previous request is being dispatched.
    pendingOfflineControls.fetch_or(4u, std::memory_order_release);
    triggerAsyncUpdate();
}

bool PluginProcessor::isOfflineAvailable() const noexcept
{
    // An ARA binding confirms host support. REAPER additionally exposes offline
    // analysis through its project bridge, including the Monitoring FX chain.
    return isBoundToARA() || isOfflineLocationAvailable();
}

bool PluginProcessor::isOfflineMode() const noexcept
{
    const auto* mode = parameters.getRawParameterValue(analysisModeParameterId);
    return isOfflineAvailable() && mode != nullptr && mode->load(std::memory_order_relaxed) >= 0.5f;
}

void PluginProcessor::setOfflineMode(const bool offline)
{
    auto* mode = parameters.getParameter(analysisModeParameterId);
    if (mode == nullptr)
        return;
    const auto value = offline && isOfflineAvailable() ? 1.0f : 0.0f;
    if (juce::approximatelyEqual(mode->getValue(), value))
        return;
    mode->beginChangeGesture();
    mode->setValueNotifyingHost(value);
    mode->endChangeGesture();
    if (isOfflineMode())
        forceOfflineRefresh();
    else
        cancelOfflineAnalysis();
    notifyHostOfStateChange();
}
