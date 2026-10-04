#pragma once

#include "offline/shell/SourceChoice.h"

#include <JuceHeader.h>


#include <vector>

namespace ana::offline
{
std::vector<juce::ARAPlaybackRegion*> collectAnalysisRegions(
    ARA::PlugIn::DocumentController* documentController,
    const std::vector<juce::ARAPlaybackRegion*>& rendererRegions);

std::vector<offline::SourceChoice> makeSourceChoices(
    const std::vector<juce::ARAPlaybackRegion*>& playbackRegions);

bool matchesSelection(const juce::ARAPlaybackRegion& playbackRegion,
                      const std::vector<offline::SourceChoice>& choices,
                             const juce::String& sourceId,
                             int takeNumber);

juce::ARAAudioModification* findTakeModification(
    ARA::PlugIn::DocumentController* documentController,
    const std::vector<offline::SourceChoice>& choices,
    const juce::String& sourceId,
    int takeNumber);

juce::ARAAudioSource* findHostTakeAudioSource(
    ARA::PlugIn::DocumentController* documentController,
    const offline::SourceChoice& choice);

std::unique_ptr<juce::AudioFormatReader> createHostTakeReader(
    ARA::PlugIn::DocumentController* documentController,
    const offline::SourceChoice& choice);
}
