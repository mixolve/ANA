#pragma once

#include "shell/AraAnalysisRequest.h"
#include "shell/AraAnalysisResult.h"

#include <JuceHeader.h>

#if JucePlugin_Enable_ARA

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace ana::ara
{
std::shared_ptr<ara::AnalysisResult> analysePlaybackRegions(
    ARA::PlugIn::DocumentController* documentController,
    const std::vector<juce::ARAPlaybackRegion*>& playbackRegions,
    const ara::AnalysisRequest& request,
    uint64_t analysisRevision,
    const std::function<bool()>& shouldCancel,
    const std::function<void(float)>& onProgress);
}

#endif
