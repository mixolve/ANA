#pragma once

#include <JuceHeader.h>

#include <cstdint>

namespace ana::ara
{
struct SourceChoice
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

    bool operator==(const SourceChoice& other) const
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
}
