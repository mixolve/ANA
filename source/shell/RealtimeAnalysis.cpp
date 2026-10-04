#include "Processor.h"
#include "shared/spec/SpectrumProcessing.h"

#include <cmath>

void PluginProcessor::prepareToPlay(const double sampleRate, const int samplesPerBlock)
{
    multibandScop.prepare(sampleRate);
    specProcessor.prepare(sampleRate);
    corrProcessor.prepare(sampleRate);
    lvlsProcessor.prepare(sampleRate, samplesPerBlock);
    lvlsLoudnessProcessor.prepare(sampleRate, samplesPerBlock);
    lvlsHistoryProcessor.prepare(sampleRate, samplesPerBlock);
    prepareToPlayForARA(sampleRate, samplesPerBlock, getMainBusNumOutputChannels(), getProcessingPrecision());
    processingOffline = isOfflineMode();
}

void PluginProcessor::releaseResources()
{
    releaseResourcesForARA();
    multibandScop.reset();
    specProcessor.reset();
    corrProcessor.reset();
    lvlsProcessor.reset();
    lvlsLoudnessProcessor.reset();
    lvlsHistoryProcessor.reset();
}

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    processBlockForARA(buffer, isRealtime(), getPlayHead());
    const auto offline = isOfflineMode();
    if (processingOffline != offline)
    {
        processingOffline = offline;
        // Only the audio thread resets streaming DSP during mode changes.
        multibandScop.reset();
        specProcessor.reset();
        corrProcessor.reset();
        lvlsProcessor.reset();
        lvlsLoudnessProcessor.reset();
        lvlsHistoryProcessor.reset();
        hostWasPlaying = false;
    }
    if (offline)
        return;

    auto* playHead = getPlayHead();
    auto hostIsPlaying = false;
    auto hostTransportStateAvailable = false;
    if (playHead != nullptr)
        if (const auto position = playHead->getPosition())
        {
            if (const auto bpm = position->getBpm(); bpm.hasValue()
                && std::isfinite(*bpm) && *bpm > 0.0)
                hostTempoBpm.store(*bpm, std::memory_order_relaxed);
            hostTransportStateAvailable = true;
            hostIsPlaying = position->getIsPlaying();
        }

    const auto transportStarted = hostTransportStateAvailable
        && hostIsPlaying
        && ! hostWasPlaying;

    const auto& fftSizes = ana::fft::StereoFftStream::supportedFftSizes;
    const auto fftSizeChoice = [this] (const char* parameterId)
    {
        const auto* value = getRawParameterValue(parameterId);
        const auto index = value != nullptr
            ? juce::roundToInt(value->load(std::memory_order_relaxed))
            : static_cast<int>(ana::fft::StereoFftStream::defaultFftSizeIndex);
        return juce::jlimit(0,
                            static_cast<int>(ana::fft::StereoFftStream::supportedFftSizes.size()) - 1,
                            index);
    };

    switch (getAnalyzerPageState())
    {
        case ana::AnalyzerPage::scop:
            multibandScop.setCrossoverSettings(getCrossoverCount(), getCrossoverFrequencies());
            multibandScop.processBlock(buffer);
            break;

        case ana::AnalyzerPage::spec:
        {
            const auto fftSizeIndex = fftSizeChoice(specFftSizeParameterId);
            const auto* fftOverlapValue = getRawParameterValue(specFftOverlapParameterId);
            const auto* averagingTime = getRawParameterValue(specAverageTimeParameterId);
            const auto* mapTimeOverlap = getRawParameterValue(specMapTimeOverlapParameterId);
            const auto* clearOnPlay = getRawParameterValue(specClearOnPlayParameterId);
            if (transportStarted)
            {
                if (clearOnPlay != nullptr && clearOnPlay->load(std::memory_order_relaxed) >= 0.5f)
                    specProcessor.requestClear();
                else
                    specProcessor.requestRealtimeReset();
            }
            specProcessor.processBlock(
                buffer, fftSizes[static_cast<size_t>(fftSizeIndex)],
                fftOverlapValue != nullptr ? fftOverlapValue->load(std::memory_order_relaxed)
                                           : ana::fft::StereoFftStream::defaultOverlap,
                averagingTime != nullptr ? averagingTime->load(std::memory_order_relaxed)
                                         : ana::spectrum_processing::defaultAveragingTimeMilliseconds,
                mapTimeOverlap != nullptr
                    ? juce::roundToInt(mapTimeOverlap->load(std::memory_order_relaxed)) : 0);
            break;
        }

        case ana::AnalyzerPage::corr:
        {
            const auto fftSizeIndex = fftSizeChoice(corrFftSizeParameterId);
            const auto* fftOverlapValue = getRawParameterValue(corrFftOverlapParameterId);
            const auto* averagingTime = getRawParameterValue(corrAverageTimeParameterId);
            const auto* mode = getRawParameterValue(corrModeParameterId);
            const auto* clearOnPlay = getRawParameterValue(corrClearOnPlayParameterId);
            if (clearOnPlay != nullptr && clearOnPlay->load(std::memory_order_relaxed) >= 0.5f
                && transportStarted)
                corrProcessor.requestClear();
            const auto modeIndex = mode != nullptr
                ? juce::jlimit(0, ana::corr::CorrProcessor::modeCount - 1,
                             juce::roundToInt(mode->load(std::memory_order_relaxed)))
                : 0;
            const auto corrMode = ana::corr::CorrProcessor::modeFromIndex(modeIndex);
            corrProcessor.processBlock(
                buffer, fftSizes[static_cast<size_t>(fftSizeIndex)],
                fftOverlapValue != nullptr ? fftOverlapValue->load(std::memory_order_relaxed)
                                           : ana::fft::StereoFftStream::defaultOverlap,
                averagingTime != nullptr ? averagingTime->load(std::memory_order_relaxed)
                                         : ana::spectrum_processing::defaultAveragingTimeMilliseconds,
                corrMode);
            break;
        }

        case ana::AnalyzerPage::lvls:
        {
            if (transportStarted)
            {
                if (isParameterEnabled(lvlsClearOnPlayParameterId))
                    lvlsProcessor.requestClear();
                if (isParameterEnabled(lvlsLoudnessClearOnPlayParameterId))
                    lvlsLoudnessProcessor.requestClear();
                if (isParameterEnabled(lvlsHistoryClearOnPlayParameterId))
                    lvlsHistoryProcessor.requestClear();
            }
            const auto* rmsWindow = getRawParameterValue(lvlsRmsWindowMsParameterId);
            const auto* peakHoldTime = getRawParameterValue(lvlsPeakHoldMsParameterId);
            const auto hold = hostTransportStateAvailable && ! hostIsPlaying;
            lvlsProcessor.processBlock(buffer,
                rmsWindow != nullptr ? rmsWindow->load(std::memory_order_relaxed)
                                     : ana::lvls::defaultRmsWindowMilliseconds,
                peakHoldTime != nullptr ? peakHoldTime->load(std::memory_order_relaxed)
                                        : ana::lvls::defaultPeakHoldMilliseconds,
                hold, { isParameterEnabled(lvlsPeakRmsVisibleParameterId), false, false });
            lvlsLoudnessProcessor.processBlock(buffer,
                ana::lvls::defaultRmsWindowMilliseconds, ana::lvls::defaultPeakHoldMilliseconds,
                hold, { false, isParameterEnabled(lvlsLoudnessVisibleParameterId), false });
            const auto historyVisible = isParameterEnabled(lvlsHistoryVisibleParameterId);
            lvlsHistoryProcessor.processBlock(buffer,
                ana::lvls::defaultRmsWindowMilliseconds, ana::lvls::defaultPeakHoldMilliseconds,
                hold, { historyVisible, false, historyVisible });
            break;
        }

        default:
            break;
    }

    if (hostTransportStateAvailable)
        hostWasPlaying = hostIsPlaying;

    for (auto channel = getTotalNumInputChannels(); channel < getTotalNumOutputChannels(); ++channel)
        buffer.clear(channel, 0, buffer.getNumSamples());
}
