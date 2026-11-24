#pragma once

#include "shell/Processor.h"
#include "shared/spec/MonitorMode.h"

namespace ana::spec::realtime_detail
{
inline bool shouldUseSplitView(const PluginProcessor& processor,
                        const ana::spec::MonitorMode mode,
                        const bool mapMode) noexcept
{
    if (! ana::spec::supportsSplitView(mode))
        return false;

    if (mapMode)
        return true;

    const auto* split = processor.getRawParameterValue(
        PluginProcessor::specSplitViewParameterId);
    return split != nullptr && split->load(std::memory_order_relaxed) >= 0.5f;
}
}
