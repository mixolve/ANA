#pragma once

#include "../scop/LinkwitzRileyCrossover.h"
#include "../lvls/MeterProcessor.h"

#include <JuceHeader.h>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace ana
{
namespace freq
{
class SpectrumProcessor;
}

namespace corr
{
class StereoProcessor;
}

struct OfflineEnvelope
{
    std::vector<float> minimums;
    std::vector<float> maximums;
};

struct OfflineAnalysisSnapshot
{
    static constexpr size_t numChannelModes = 4;
    using BandEnvelopes = std::array<OfflineEnvelope, numChannelModes>;

    std::array<BandEnvelopes, dsp::Crossover::numRanges> bands;
    BandEnvelopes wideband;
    size_t activeBandCount = 0;
    double startTimeSeconds = 0.0;
    double durationSeconds = 0.0;
    uint64_t revision = 0;
    std::shared_ptr<freq::SpectrumProcessor> spectrum;
    double frequencySampleRate = 0.0;
    int frequencyBlockSize = 4096;
    float frequencyOverlap = 0.75f;
    float frequencyAveragingTimeMilliseconds = 500.0f;
    std::shared_ptr<corr::StereoProcessor> correlation;
    double correlationSampleRate = 0.0;
    int correlationBlockSize = 4096;
    float correlationOverlap = 0.75f;
    float correlationAveragingTimeMilliseconds = 500.0f;
    std::shared_ptr<lvls::MeterProcessor> meters;
    double levelSampleRate = 0.0;
};

struct OfflineSourceTakeChoice
{
    juce::String sourceId;
    juce::String sourceName;
    juce::String takeId;
    juce::String takeName;
    int takeNumber = 1;
    juce::String audioSourceName;
    juce::String audioSourcePersistentId;
    double playbackStartSeconds = 0.0;
    double playbackDurationSeconds = 0.0;
    double sourceStartSeconds = 0.0;
    double playRate = 1.0;
    double gain = 1.0;
    bool hostEnumerated = false;
    bool activeTake = false;
    uint64_t audioContentFingerprint = 0;
    bool hasAudioContentFingerprint = false;

    bool operator==(const OfflineSourceTakeChoice& other) const
    {
        return sourceId == other.sourceId
            && sourceName == other.sourceName
            && takeId == other.takeId
            && takeName == other.takeName
            && takeNumber == other.takeNumber
            && audioSourceName == other.audioSourceName
            && audioSourcePersistentId == other.audioSourcePersistentId
            && juce::approximatelyEqual(playbackStartSeconds, other.playbackStartSeconds)
            && juce::approximatelyEqual(playbackDurationSeconds, other.playbackDurationSeconds)
            && juce::approximatelyEqual(sourceStartSeconds, other.sourceStartSeconds)
            && juce::approximatelyEqual(playRate, other.playRate)
            && juce::approximatelyEqual(gain, other.gain)
            && hostEnumerated == other.hostEnumerated
            && activeTake == other.activeTake
            && audioContentFingerprint == other.audioContentFingerprint
            && hasAudioContentFingerprint == other.hasAudioContentFingerprint;
    }
};

struct OfflineAnalysisRequest
{
    dsp::Crossover::SplitFrequencies frequencies {};
    size_t activeSplitCount = 0;
    size_t columnCount = 512;
    int frequencyBlockSize = 4096;
    float frequencyOverlap = 0.75f;
    float frequencyAveragingTimeMilliseconds = 500.0f;
    int correlationBlockSize = 4096;
    float correlationOverlap = 0.75f;
    float correlationAveragingTimeMilliseconds = 500.0f;
    juce::String sourceId;
    juce::String takeId;
    std::vector<OfflineSourceTakeChoice> sourceTakeChoices;
    int analyzerPage = 0;
    lvls::MeterProcessor::ProcessingOptions levelOptions;
    uint64_t regionGeneration = 0;
    uint64_t revision = 0;

    bool hasSameSettings(const OfflineAnalysisRequest& other) const
    {
        return activeSplitCount == other.activeSplitCount
            && frequencies == other.frequencies
            && columnCount == other.columnCount
            && frequencyBlockSize == other.frequencyBlockSize
            && juce::approximatelyEqual(frequencyOverlap, other.frequencyOverlap)
            && juce::approximatelyEqual(frequencyAveragingTimeMilliseconds,
                                         other.frequencyAveragingTimeMilliseconds)
            && correlationBlockSize == other.correlationBlockSize
            && juce::approximatelyEqual(correlationOverlap, other.correlationOverlap)
            && juce::approximatelyEqual(correlationAveragingTimeMilliseconds,
                                         other.correlationAveragingTimeMilliseconds)
            && sourceId == other.sourceId
            && takeId == other.takeId
            && sourceTakeChoices == other.sourceTakeChoices
            && analyzerPage == other.analyzerPage
            && levelOptions.peakRms == other.levelOptions.peakRms
            && levelOptions.loudness == other.levelOptions.loudness
            && levelOptions.history == other.levelOptions.history
            && regionGeneration == other.regionGeneration;
    }
};
} // namespace ana
