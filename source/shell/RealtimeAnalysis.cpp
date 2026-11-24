#include "Processor.h"
#include "shared/spec/AveragingAndSmoothing.h"

#include <cmath>

void PluginProcessor::prepareToPlay(const double sampleRate, const int samplesPerBlock)
{
    analysisScratch.setSize(2, juce::jmax(1, samplesPerBlock));
    multibandScop.prepare(sampleRate);
    specProcessor.prepare(sampleRate);
    corrProcessor.prepare(sampleRate);
    lvlsProcessor.prepare(sampleRate, samplesPerBlock);
    lvlsLoudnessProcessor.prepare(sampleRate, samplesPerBlock);
    lvlsMetricsProcessor.prepare(sampleRate, samplesPerBlock);
    prepareToPlayForARA(sampleRate, samplesPerBlock, getMainBusNumOutputChannels(), getProcessingPrecision());
    processingOffline = isOfflineMode();
    hostWasPlaying = false;
    hostTransportState.store(-1, std::memory_order_relaxed);
}

void PluginProcessor::resetRealtimeAnalysis()
{
    multibandScop.reset();
    specProcessor.reset();
    corrProcessor.reset();
    lvlsProcessor.reset();
    lvlsLoudnessProcessor.reset();
    lvlsMetricsProcessor.reset();
    hostWasPlaying = false;
    hostTransportState.store(-1, std::memory_order_relaxed);
}

void PluginProcessor::releaseResources()
{
    releaseResourcesForARA();
    resetRealtimeAnalysis();
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
        resetRealtimeAnalysis();
    }
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
    hostTransportState.store(hostTransportStateAvailable ? (hostIsPlaying ? 1 : 0) : -1,
                             std::memory_order_relaxed);
    if (offline)
        return;

    const auto* compensation = getRawParameterValue(compensationParameterId);
    const auto analysisGain = compensation != nullptr
        ? juce::Decibels::decibelsToGain(compensation->load(std::memory_order_relaxed), -100.0f) : 1.0f;

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

    const auto page = getAnalyzerPageState();
    const auto channels = juce::jmin(buffer.getNumChannels(), analysisScratch.getNumChannels());
    for (int offset = 0; offset < buffer.getNumSamples(); offset += analysisScratch.getNumSamples())
    {
        const auto count = juce::jmin(analysisScratch.getNumSamples(), buffer.getNumSamples() - offset);
        for (int channel = 0; channel < channels; ++channel)
        {
            analysisScratch.copyFromWithRamp(channel, 0, buffer.getReadPointer(channel, offset), count,
                                             analysisGain, analysisGain);
            // Analysis must not poison its FFT/filter history when another plug-in
            // produces NaN/Inf. The host's original audio buffer stays untouched.
            auto* samples = analysisScratch.getWritePointer(channel);
            for (int sample = 0; sample < count; ++sample)
                if (! std::isfinite(samples[sample]))
                    samples[sample] = 0.0f;
        }
        juce::AudioBuffer<float> analysisBuffer(analysisScratch.getArrayOfWritePointers(), channels, count);
        const auto chunkTransportStarted = transportStarted && offset == 0;
        switch (page)
        {
        case ana::AnalyzerPage::scop:
            multibandScop.setCrossoverSettings(getCrossoverCount(), getCrossoverFrequencies());
            if (const auto* slope = getRawParameterValue(scopSlopeParameterId))
                multibandScop.setSlope(slope->load(std::memory_order_relaxed));
            multibandScop.processBlock(analysisBuffer);
            break;

        case ana::AnalyzerPage::spec:
        {
            // Slot/host automation must switch FREQ/MAP even with the editor closed.
            specProcessor.setRealtimeMapMode(isSpecMapView());
            const auto fftSizeIndex = fftSizeChoice(specFftSizeParameterId);
            const auto* fftOverlapValue = getRawParameterValue(specFftOverlapParameterId);
            const auto* averagingTime = getRawParameterValue(specAverageTimeParameterId);
            const auto* mapTimeOverlap = getRawParameterValue(specMapTimeOverlapParameterId);
            const auto* clearOnPlay = getRawParameterValue(specClearOnPlayParameterId);
            if (chunkTransportStarted)
            {
                if (clearOnPlay != nullptr && clearOnPlay->load(std::memory_order_relaxed) >= 0.5f)
                    specProcessor.requestClear();
                else
                    specProcessor.requestRealtimeReset();
            }
            specProcessor.processBlock(
                analysisBuffer, fftSizes[static_cast<size_t>(fftSizeIndex)],
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
                && chunkTransportStarted)
                corrProcessor.requestClear();
            const auto modeIndex = mode != nullptr
                ? juce::jlimit(0, ana::corr::CorrProcessor::modeCount - 1,
                             juce::roundToInt(mode->load(std::memory_order_relaxed)))
                : 0;
            const auto corrMode = ana::corr::CorrProcessor::modeFromIndex(modeIndex);
            corrProcessor.processBlock(
                analysisBuffer, fftSizes[static_cast<size_t>(fftSizeIndex)],
                fftOverlapValue != nullptr ? fftOverlapValue->load(std::memory_order_relaxed)
                                           : ana::fft::StereoFftStream::defaultOverlap,
                averagingTime != nullptr ? averagingTime->load(std::memory_order_relaxed)
                                         : ana::spectrum_processing::defaultAveragingTimeMilliseconds,
                corrMode);
            break;
        }

        case ana::AnalyzerPage::lvls:
        {
            if (chunkTransportStarted)
            {
                if (isParameterEnabled(lvlsClearOnPlayParameterId))
                {
                    lvlsProcessor.requestClear();
                    lvlsLoudnessProcessor.requestClear();
                    lvlsMetricsProcessor.requestClear();
                }
            }
            const auto* rmsWindow = getRawParameterValue(lvlsRmsWindowMsParameterId);
            const auto* peakHoldTime = getRawParameterValue(lvlsPeakHoldMsParameterId);
            const auto hold = hostTransportStateAvailable && ! hostIsPlaying;
            const auto selectedRmsWindowMs = rmsWindow != nullptr
                ? rmsWindow->load(std::memory_order_relaxed)
                : ana::lvls::defaultRmsWindowMilliseconds;
            lvlsProcessor.processBlock(analysisBuffer,
                selectedRmsWindowMs,
                peakHoldTime != nullptr ? peakHoldTime->load(std::memory_order_relaxed)
                                        : ana::lvls::defaultPeakHoldMilliseconds,
                hold, { isParameterEnabled(lvlsPeakRmsVisibleParameterId), false, false });
            lvlsLoudnessProcessor.processBlock(analysisBuffer,
                ana::lvls::defaultRmsWindowMilliseconds, ana::lvls::defaultPeakHoldMilliseconds,
                hold, { false, isParameterEnabled(lvlsLoudnessVisibleParameterId), false });
            const auto metricsVisible = isParameterEnabled(lvlsMetricsVisibleParameterId);
            lvlsMetricsProcessor.processBlock(analysisBuffer,
                selectedRmsWindowMs, ana::lvls::defaultPeakHoldMilliseconds,
                hold, { metricsVisible, false, metricsVisible });
            break;
        }

        default:
            break;
        }
    }

    if (hostTransportStateAvailable)
        hostWasPlaying = hostIsPlaying;

    for (auto channel = getTotalNumInputChannels(); channel < getTotalNumOutputChannels(); ++channel)
        buffer.clear(channel, 0, buffer.getNumSamples());
}
