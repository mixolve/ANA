#pragma once

#include "offline/shell/AnalysisRequest.h"
#include "offline/shell/AnalysisResult.h"

#include <JuceHeader.h>


#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace ana::offline
{
std::shared_ptr<offline::AnalysisResult> analysePlaybackRegions(
    ARA::PlugIn::DocumentController* documentController,
    const std::vector<juce::ARAPlaybackRegion*>& playbackRegions,
    const offline::AnalysisRequest& request,
    uint64_t analysisRevision,
    const std::function<bool()>& shouldCancel,
    const std::function<void(float)>& onProgress);
}
