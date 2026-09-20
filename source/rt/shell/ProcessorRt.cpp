#include "Processor.h"
#include "shared/analyzer/DisplaySettings.h"

#include <cmath>

void PluginProcessor::prepareToPlay(const double sampleRate, const int samplesPerBlock)
{
    multibandScop.prepare(sampleRate);
    specProcessor.prepare(sampleRate);
    corrProcessor.prepare(sampleRate);
    lvlsProcessor.prepare(sampleRate, samplesPerBlock);
}

void PluginProcessor::releaseResources()
{
    multibandScop.reset();
    specProcessor.reset();
    corrProcessor.reset();
    lvlsProcessor.reset();
}

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

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
        const auto* value = parameters.getRawParameterValue(parameterId);
        const auto index = value != nullptr
            ? juce::roundToInt(value->load(std::memory_order_relaxed))
            : static_cast<int>(ana::fft::StereoFftStream::defaultFftSizeIndex);
        return juce::jlimit(0,
                            static_cast<int>(ana::fft::StereoFftStream::supportedFftSizes.size()) - 1,
                            index);
    };

    switch (activeAnalyzerPage.load(std::memory_order_acquire))
    {
        case ana::AnalyzerPage::scop:
            multibandScop.setCrossoverSettings(getCrossoverCount(), getCrossoverFrequencies());
            multibandScop.processBlock(buffer);
            break;

        case ana::AnalyzerPage::spec:
        {
            const auto fftSizeIndex = fftSizeChoice(specFftSizeParameterId);
            const auto* fftOverlapValue = parameters.getRawParameterValue(specFftOverlapParameterId);
            const auto* averagingTime = parameters.getRawParameterValue(specAverageTimeParameterId);
            const auto* mapTimeOverlap = parameters.getRawParameterValue(specMapTimeOverlapParameterId);
            const auto* clearOnPlay = parameters.getRawParameterValue(specClearOnPlayParameterId);
            if (transportStarted)
            {
                if (clearOnPlay != nullptr && clearOnPlay->load(std::memory_order_relaxed) >= 0.5f)
                    specProcessor.requestClear();
                else
                    specProcessor.requestRtReset();
            }
            specProcessor.processBlock(
                buffer, fftSizes[static_cast<size_t>(fftSizeIndex)],
                fftOverlapValue != nullptr ? fftOverlapValue->load(std::memory_order_relaxed)
                                           : ana::fft::StereoFftStream::defaultOverlap,
                averagingTime != nullptr ? averagingTime->load(std::memory_order_relaxed)
                                         : ana::analyzer_display::defaultAveragingTimeMilliseconds,
                mapTimeOverlap != nullptr
                    ? juce::roundToInt(mapTimeOverlap->load(std::memory_order_relaxed)) : 0);
            break;
        }

        case ana::AnalyzerPage::corr:
        {
            const auto fftSizeIndex = fftSizeChoice(corrFftSizeParameterId);
            const auto* fftOverlapValue = parameters.getRawParameterValue(corrFftOverlapParameterId);
            const auto* averagingTime = parameters.getRawParameterValue(corrAverageTimeParameterId);
            const auto* mode = parameters.getRawParameterValue(corrModeParameterId);
            const auto* clearOnPlay = parameters.getRawParameterValue(corrClearOnPlayParameterId);
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
                                         : ana::analyzer_display::defaultAveragingTimeMilliseconds,
                corrMode);
            break;
        }

        case ana::AnalyzerPage::lvls:
        {
            const auto* clearOnPlay = parameters.getRawParameterValue(lvlsClearOnPlayParameterId);
            if (clearOnPlay != nullptr && clearOnPlay->load(std::memory_order_relaxed) >= 0.5f
                && transportStarted)
                lvlsProcessor.requestClear();
            const auto* rmsWindow = parameters.getRawParameterValue(lvlsRmsWindowMsParameterId);
            const auto* peakHoldTime = parameters.getRawParameterValue(lvlsPeakHoldMsParameterId);
            const ana::lvls::LvlsProcessor::ProcessingOptions options {
                isParameterEnabled(lvlsPeakRmsVisibleParameterId),
                isParameterEnabled(lvlsLoudnessVisibleParameterId),
                isParameterEnabled(lvlsHistoryVisibleParameterId)
            };
            lvlsProcessor.processBlock(
                buffer,
                rmsWindow != nullptr ? rmsWindow->load(std::memory_order_relaxed)
                                     : ana::lvls::defaultRmsWindowMilliseconds,
                peakHoldTime != nullptr ? peakHoldTime->load(std::memory_order_relaxed)
                                        : ana::lvls::defaultPeakHoldMilliseconds,
                hostTransportStateAvailable && ! hostIsPlaying,
                options);
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
