#pragma once

#include "shell/AraSourceChoice.h"

#include <JuceHeader.h>

#if JucePlugin_Enable_ARA

#include <vector>

namespace ana::ara
{
std::vector<juce::ARAPlaybackRegion*> collectAnalysisRegions(
    ARA::PlugIn::DocumentController* documentController,
    const std::vector<juce::ARAPlaybackRegion*>& rendererRegions);

std::vector<ara::SourceChoice> makeSourceChoices(
    const std::vector<juce::ARAPlaybackRegion*>& playbackRegions);

bool matchesSelection(const juce::ARAPlaybackRegion& playbackRegion,
                      const std::vector<ara::SourceChoice>& choices,
                             const juce::String& sourceId,
                             int takeNumber);

juce::ARAAudioModification* findTakeModification(
    ARA::PlugIn::DocumentController* documentController,
    const std::vector<ara::SourceChoice>& choices,
    const juce::String& sourceId,
    int takeNumber);

juce::ARAAudioSource* findHostTakeAudioSource(
    ARA::PlugIn::DocumentController* documentController,
    const ara::SourceChoice& choice);
}

#endif
