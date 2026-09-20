#include "Processor.h"
#include "shell/AraAnalysisRequest.h"
#include "shell/AraAnalysisResult.h"
#if JucePlugin_Enable_ARA
#include "EditorRenderer.h"
#include "PlaybackRenderer.h"
#endif

#include <algorithm>


void PluginProcessor::prepareToPlay(const double sampleRate, const int samplesPerBlock)
{
#if JucePlugin_Enable_ARA
    prepareToPlayForARA(sampleRate, samplesPerBlock, getMainBusNumOutputChannels(),
                        getProcessingPrecision());
#else
    juce::ignoreUnused(sampleRate, samplesPerBlock);
#endif
}

void PluginProcessor::releaseResources()
{
#if JucePlugin_Enable_ARA
    releaseResourcesForARA();
#endif
}

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

#if JucePlugin_Enable_ARA
    processBlockForARA(buffer, isRealtime(), getPlayHead());
#endif

    for (auto channel = getTotalNumInputChannels(); channel < getTotalNumOutputChannels(); ++channel)
        buffer.clear(channel, 0, buffer.getNumSamples());
}

bool PluginProcessor::isARAAvailable() const noexcept
{
#if JucePlugin_Enable_ARA
    return isBoundToARA() && (isPlaybackRenderer() || isEditorRenderer());
#else
    return false;
#endif
}

int PluginProcessor::getAraAnalysisProgress() const noexcept
{
#if JucePlugin_Enable_ARA
    if (auto* renderer = getEditorRenderer<ana::ara::EditorRenderer>())
        return renderer->getAnalysisProgress();
    if (auto* renderer = getPlaybackRenderer<ana::ara::PlaybackRenderer>())
        return renderer->getAnalysisProgress();
#endif

    return 0;
}

void PluginProcessor::requestAraAnalysis(const size_t columnCount, const bool forceRefresh,
                                             const bool specMapMode, const size_t specMapRowCount)
{
#if JucePlugin_Enable_ARA
    const auto crossoverCount = getCrossoverCount();
    const auto frequencies = getCrossoverFrequencies();
    const auto sourceId = getSelectedAraSourceId();
    const auto takeNumber = getSelectedAraTakeNumber();
    const auto sourceChoices = getAraSourceChoices();
    const auto& fftSizes = ana::fft::StereoFftStream::supportedFftSizes;
    const auto* specFftSizeValue = parameters.getRawParameterValue(specFftSizeParameterId);
    const auto specFftSizeIndex = juce::jlimit(0, static_cast<int>(fftSizes.size()) - 1,
                                             specFftSizeValue != nullptr
                                                 ? juce::roundToInt(specFftSizeValue->load(std::memory_order_relaxed))
                                                 : static_cast<int>(ana::fft::StereoFftStream::defaultFftSizeIndex));
    const auto* specFftOverlapValue = parameters.getRawParameterValue(specFftOverlapParameterId);
    const auto specFftOverlap = specFftOverlapValue != nullptr ? specFftOverlapValue->load(std::memory_order_relaxed)
                                       : ana::fft::StereoFftStream::defaultOverlap;
    const auto* mapTimeOverlapValue = parameters.getRawParameterValue(specMapTimeOverlapParameterId);
    const auto mapTimeOverlapChoice = juce::roundToInt(
        mapTimeOverlapValue != nullptr ? mapTimeOverlapValue->load(std::memory_order_relaxed) : 0.0f);
    const auto specMapTimeOverlapFraction = ana::spec::SpecProcessor::mapTimeOverlapFraction(mapTimeOverlapChoice);
    const auto* corrFftSizeValue = parameters.getRawParameterValue(corrFftSizeParameterId);
    const auto corrFftSizeIndex = juce::jlimit(0, static_cast<int>(fftSizes.size()) - 1,
        corrFftSizeValue != nullptr
            ? juce::roundToInt(corrFftSizeValue->load(std::memory_order_relaxed)) : static_cast<int>(ana::fft::StereoFftStream::defaultFftSizeIndex));
    const auto* corrFftOverlapValue = parameters.getRawParameterValue(corrFftOverlapParameterId);
    const auto corrFftOverlap = corrFftOverlapValue != nullptr
        ? corrFftOverlapValue->load(std::memory_order_relaxed)
        : ana::fft::StereoFftStream::defaultOverlap;
    const auto analyzerPage = getAnalyzerPageState();
    const auto includeLvlsPeakRms = isParameterEnabled(lvlsPeakRmsVisibleParameterId);
    const auto includeLvlsLoudness = isParameterEnabled(lvlsLoudnessVisibleParameterId);
    const auto includeLvlsHistory = isParameterEnabled(lvlsHistoryVisibleParameterId);
    const auto* lvlsRmsWindow = parameters.getRawParameterValue(lvlsRmsWindowMsParameterId);
    const auto* lvlsPeakHold = parameters.getRawParameterValue(lvlsPeakHoldMsParameterId);

    ana::ara::AnalysisRequest request;
    request.crossoverCount = std::min(crossoverCount, ana::dsp::LinkwitzRileyCrossover::numCrossovers);
    request.frequencies = frequencies;
    request.columnCount = std::max<size_t>(1, columnCount);
    request.specFftSize = fftSizes[static_cast<size_t>(specFftSizeIndex)];
    request.specMapMode = analyzerPage == ana::AnalyzerPage::spec && specMapMode;
    request.specMapRowCount = std::clamp<size_t>(
        specMapRowCount,
        ana::ara::SpectrogramMap::minimumRowCount,
        ana::ara::SpectrogramMap::maximumRowCount);
    request.specFftOverlap = specFftOverlap;
    request.specMapTimeOverlapFraction = specMapTimeOverlapFraction;
    request.corrFftSize = fftSizes[static_cast<size_t>(corrFftSizeIndex)];
    request.corrFftOverlap = corrFftOverlap;
    if (const auto* mode = parameters.getRawParameterValue(corrModeParameterId))
        request.corrMode = juce::jlimit(0, ana::corr::CorrProcessor::modeCount - 1,
                                 juce::roundToInt(mode->load(std::memory_order_relaxed)));
    request.sourceId = sourceId;
    request.takeNumber = takeNumber;
    request.sourceChoices = sourceChoices;
    request.analyzerPage = analyzerPage;
    request.includeLvlsPeakRms = includeLvlsPeakRms;
    request.includeLvlsLoudness = includeLvlsLoudness;
    request.includeLvlsHistory = includeLvlsHistory;
    request.lvlsRmsWindowMs = lvlsRmsWindow != nullptr
        ? lvlsRmsWindow->load(std::memory_order_relaxed) : ana::lvls::defaultRmsWindowMilliseconds;
    request.lvlsPeakHoldMs = lvlsPeakHold != nullptr
        ? lvlsPeakHold->load(std::memory_order_relaxed) : ana::lvls::defaultPeakHoldMilliseconds;

    if (auto* renderer = getPlaybackRenderer<ana::ara::PlaybackRenderer>())
        renderer->requestAraAnalysis(request, forceRefresh);

    if (auto* renderer = getEditorRenderer<ana::ara::EditorRenderer>())
        renderer->requestAraAnalysis(std::move(request), forceRefresh);
#else
    juce::ignoreUnused(columnCount, forceRefresh, specMapMode, specMapRowCount);
#endif
}

std::shared_ptr<const ana::ara::AnalysisResult> PluginProcessor::getAraAnalysisResult() const
{
#if JucePlugin_Enable_ARA
    std::shared_ptr<const ana::ara::AnalysisResult> playbackAnalysis;

    if (auto* renderer = getPlaybackRenderer<ana::ara::PlaybackRenderer>())
        playbackAnalysis = renderer->getAraAnalysisResult();

    if (auto* renderer = getEditorRenderer<ana::ara::EditorRenderer>())
    {
        auto editorAnalysis = renderer->getAraAnalysisResult();

        if (editorAnalysis != nullptr && editorAnalysis->durationSeconds > 0.0)
            return editorAnalysis;
    }

    return playbackAnalysis;
#endif

    return {};
}

juce::Image PluginProcessor::getCachedAraSpectrogramImage() const
{
    const juce::ScopedLock scopedLock(araSpectrogramImageLock);
    return cachedAraSpectrogramImage;
}

void PluginProcessor::setCachedAraSpectrogramImage(const juce::Image& image)
{
    if (! image.isValid())
        return;

    const juce::ScopedLock scopedLock(araSpectrogramImageLock);
    cachedAraSpectrogramImage = image;
}

std::vector<ana::ara::SourceChoice> PluginProcessor::getAraSourceChoices() const
{
    auto reaperChoices = reaperHostBridge.getTakeChoices();
    if (! reaperChoices.empty())
        return reaperChoices;

#if JucePlugin_Enable_ARA
    if (auto* renderer = getEditorRenderer<ana::ara::EditorRenderer>())
    {
        auto choices = renderer->getAraSourceChoices();

        if (! choices.empty())
            return choices;
    }

    if (auto* renderer = getPlaybackRenderer<ana::ara::PlaybackRenderer>())
        return renderer->getAraSourceChoices();
#endif

    return {};
}

juce::String PluginProcessor::getSelectedAraSourceId() const
{
    const juce::ScopedLock scopedLock(araSelectionLock);
    return selectedAraSourceId;
}

int PluginProcessor::getSelectedAraTakeNumber() const noexcept
{
    const juce::ScopedLock scopedLock(araSelectionLock);
    return selectedAraTakeNumber;
}

void PluginProcessor::setSelectedAraSourceId(const juce::String& sourceId)
{
    {
        const juce::ScopedLock scopedLock(araSelectionLock);
        if (selectedAraSourceId == sourceId)
            return;
        selectedAraSourceId = sourceId;
    }

    if (sourceId.isEmpty())
        parameters.state.removeProperty(araSourceStateKey, nullptr);
    else
        parameters.state.setProperty(araSourceStateKey, sourceId, nullptr);

    updateHostDisplay(juce::AudioProcessorListener::ChangeDetails()
                          .withNonParameterStateChanged(true));
}

void PluginProcessor::setSelectedAraTakeNumber(const int takeNumber)
{
    const auto validTakeNumber = juce::jmax(0, takeNumber);
    {
        const juce::ScopedLock scopedLock(araSelectionLock);
        if (selectedAraTakeNumber == validTakeNumber)
            return;
        selectedAraTakeNumber = validTakeNumber;
    }

    if (validTakeNumber == 0)
        parameters.state.removeProperty(araTakeNumberStateKey, nullptr);
    else
        parameters.state.setProperty(araTakeNumberStateKey, validTakeNumber, nullptr);

    updateHostDisplay(juce::AudioProcessorListener::ChangeDetails()
                          .withNonParameterStateChanged(true));
}
