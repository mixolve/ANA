#include "Processor.h"

bool PluginProcessor::isOfflineAvailable() const noexcept
{
    // An ARA binding confirms host support. REAPER additionally exposes offline
    // analysis through its project bridge, including the Monitoring FX chain.
    return isBoundToARA() || isOfflineLocationAvailable();
}

bool PluginProcessor::isOfflineMode() const noexcept
{
    const auto* mode = parameters.getRawParameterValue(analysisModeParameterId);
    return isOfflineAvailable() && mode != nullptr && mode->load(std::memory_order_relaxed) >= 0.5f;
}

void PluginProcessor::setOfflineMode(const bool offline)
{
    auto* mode = parameters.getParameter(analysisModeParameterId);
    if (mode == nullptr)
        return;
    const auto value = offline && isOfflineAvailable() ? 1.0f : 0.0f;
    if (juce::approximatelyEqual(mode->getValue(), value))
        return;
    mode->beginChangeGesture();
    mode->setValueNotifyingHost(value);
    mode->endChangeGesture();
    if (isOfflineMode())
        forceOfflineRefresh();
    else
        cancelOfflineAnalysis();
    updateHostDisplay(juce::AudioProcessorListener::ChangeDetails().withNonParameterStateChanged(true));
}
