#include "Processor.h"
#include "LayoutWeights.h"
#include "shared/scop/Settings.h"
#include "shared/spec/Settings.h"
#include "shared/corr/Settings.h"

using ana::processor_detail::normalizeLayoutWeights;

#include <algorithm>
#include <cmath>

void PluginProcessor::setSpecMonitorMode(const int mode)
{
    const auto nextMode = juce::jlimit(
        0, static_cast<int>(ana::spec::monitorModeCount) - 1, mode);
    auto* modeParameter = getActiveParameter(specMonitorModeParameterId);
    if (modeParameter == nullptr)
        return;

    const auto previousMode = juce::roundToInt(modeParameter->getValue()
                                               * static_cast<float>(modeParameter->getNumSteps() - 1));
    if (previousMode == nextMode)
        return;

    modeParameter->setValueNotifyingHost(modeParameter->convertTo0to1(static_cast<float>(nextMode)));
    notifyHostOfStateChange();
}

void PluginProcessor::setCorrMode(const int mode)
{
    const auto nextMode = juce::jlimit(0, ana::corr::CorrProcessor::modeCount - 1, mode);
    auto* modeParameter = getActiveParameter(corrModeParameterId);
    if (modeParameter == nullptr)
        return;

    const auto previousMode = juce::roundToInt(modeParameter->getValue()
                                               * static_cast<float>(modeParameter->getNumSteps() - 1));
    if (previousMode == nextMode)
        return;

    modeParameter->setValueNotifyingHost(modeParameter->convertTo0to1(static_cast<float>(nextMode)));
    constrainCorrRangeForMode(nextMode);
    notifyHostOfStateChange();
}

void PluginProcessor::constrainCorrRangeForMode(const int mode)
{
    if (mode != ana::corr::CorrProcessor::modeIndex(
                    ana::corr::CorrProcessor::Mode::frequency))
        return;

    auto* lowParameter = getActiveParameter(corrRangeLowParameterId);
    auto* highParameter = getActiveParameter(corrRangeHighParameterId);
    const auto* lowValue = getRawParameterValue(corrRangeLowParameterId);
    const auto* highValue = getRawParameterValue(corrRangeHighParameterId);
    if (lowParameter == nullptr || highParameter == nullptr || lowValue == nullptr || highValue == nullptr)
        return;

    auto low = juce::jlimit(ana::corr::frequencyModeMinimumCoefficient,
                            ana::corr::maximumCoefficient,
                            lowValue->load(std::memory_order_relaxed));
    auto high = juce::jlimit(ana::corr::frequencyModeMinimumCoefficient,
                             ana::corr::maximumCoefficient,
                             highValue->load(std::memory_order_relaxed));
    if (high < low + ana::corr::minimumCoefficientSpan)
    {
        if (low <= ana::corr::maximumCoefficient - ana::corr::minimumCoefficientSpan)
            high = low + ana::corr::minimumCoefficientSpan;
        else
        {
            low = ana::corr::maximumCoefficient - ana::corr::minimumCoefficientSpan;
            high = ana::corr::maximumCoefficient;
        }
    }

    const auto lowNormalized = lowParameter->convertTo0to1(low);
    const auto highNormalized = highParameter->convertTo0to1(high);
    if (! juce::approximatelyEqual(lowParameter->getValue(), lowNormalized))
        lowParameter->setValueNotifyingHost(lowNormalized);
    if (! juce::approximatelyEqual(highParameter->getValue(), highNormalized))
        highParameter->setValueNotifyingHost(highNormalized);
}

void PluginProcessor::setLastEditorSize(const int width, const int height) noexcept
{
    const auto validWidth = std::max(0, width);
    const auto validHeight = std::max(0, height);

    if (lastEditorWidth.load(std::memory_order_relaxed) == validWidth
        && lastEditorHeight.load(std::memory_order_relaxed) == validHeight
        && static_cast<int>(parameters.state.getProperty(editorWidthStateKey, 0)) == validWidth
        && static_cast<int>(parameters.state.getProperty(editorHeightStateKey, 0)) == validHeight)
        return;

    lastEditorWidth.store(validWidth, std::memory_order_relaxed);
    lastEditorHeight.store(validHeight, std::memory_order_relaxed);

    if (validWidth > 0 && validHeight > 0)
    {
        parameters.state.setProperty(editorWidthStateKey, validWidth, nullptr);
        parameters.state.setProperty(editorHeightStateKey, validHeight, nullptr);
    }
}

void PluginProcessor::setLvlsSectionWeights(const std::array<float, 3>& weights)
{
    auto safeWeights = weights;
    for (auto& weight : safeWeights)
    {
        weight = std::isfinite(weight) ? std::max(0.001f, weight) : 1.0f;
    }

    normalizeLayoutWeights(safeWeights);

    const auto current = getLvlsSectionWeights();
    for (size_t index = 0; index < safeWeights.size(); ++index)
    {
        if (std::abs(current[index] - safeWeights[index]) <= 1.0e-5f)
            continue;

        parameters.state.setProperty(lvlsSectionWeightStateKeys[index], safeWeights[index], nullptr);
    }
}

void PluginProcessor::setScopBandHeightWeights(
    const std::array<float, ana::dsp::LinkwitzRileyCrossover::numBands>& weights)
{
    auto safeWeights = weights;
    for (auto& weight : safeWeights)
    {
        weight = std::isfinite(weight) ? std::max(0.001f, weight) : 1.0f;
    }
    normalizeLayoutWeights(safeWeights);

    const auto current = getScopBandHeightWeights();
    for (size_t index = 0; index < safeWeights.size(); ++index)
    {
        if (std::abs(current[index] - safeWeights[index]) <= 1.0e-5f)
            continue;
        parameters.state.setProperty(scopBandHeightStateKeys[index], safeWeights[index], nullptr);
    }
}

void PluginProcessor::setAnalyzerPageState(const ana::AnalyzerPage page)
{
    if (getAnalyzerPageState() == page)
        return;

    if (auto* module = parameters.getParameter(moduleParameterId))
        module->setValueNotifyingHost(module->convertTo0to1(static_cast<float>(page)));
    notifyHostOfStateChange();
}

void PluginProcessor::setSpecMapView(const bool shouldUseMap)
{
    auto* view = parameters.getParameter(specViewParameterId);
    if (view == nullptr || isSpecMapView() == shouldUseMap)
        return;
    view->setValueNotifyingHost(view->convertTo0to1(shouldUseMap ? 1.0f : 0.0f));
    notifyHostOfStateChange();
}

ana::dsp::LinkwitzRileyCrossover::CrossoverFrequencies PluginProcessor::getCrossoverFrequencies() const noexcept
{
    auto frequencies = ana::dsp::LinkwitzRileyCrossover::defaultFrequencies;

    for (size_t index = 0; index < frequencies.size(); ++index)
    {
        if (const auto* value = getRawParameterValue(crossoverParameterIds[index]))
            frequencies[index] = static_cast<double>(value->load(std::memory_order_relaxed));
    }

    return frequencies;
}

void PluginProcessor::setCrossoverCount(const size_t crossoverCount)
{
    auto* parameter = getActiveParameter(crossoverCountParameterId);
    if (parameter == nullptr)
        return;

    const auto plainValue = static_cast<float>(std::min(crossoverCount, ana::dsp::LinkwitzRileyCrossover::numCrossovers));
    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost(parameter->convertTo0to1(plainValue));
    parameter->endChangeGesture();
}

void PluginProcessor::setScopSingleViewBand(const int bandIndex)
{
    const auto validBand = juce::jlimit(-1,
                                        static_cast<int>(ana::dsp::LinkwitzRileyCrossover::numBands) - 1,
                                        bandIndex);

    if (getScopSingleViewBand() == validBand)
        return;

    parameters.state.setProperty(scopSingleViewStateKey, validBand, nullptr);
}

void PluginProcessor::setScopFullSourceView(const bool shouldShowFullSource)
{
    auto* parameter=parameters.getParameter(scopOverallFullParameterId);
    const auto value=shouldShowFullSource?1.0f:0.0f;
    if(!juce::approximatelyEqual(parameter->getValue(),value)) parameter->setValueNotifyingHost(value);
    // Clear the single-band selection even when the host has already updated
    // the APVTS full-source parameter before the deferred UI action runs.
    if (shouldShowFullSource && getScopSingleViewBand() >= 0)
        parameters.state.setProperty(scopSingleViewStateKey, -1, nullptr);
}

void PluginProcessor::setScopChannelMode(const size_t bandIndex, const ana::ScopChannelMode mode)
{
    if (bandIndex >= scopChannelModeParameterIds.size())
        return;

    auto* parameter = getActiveParameter(scopChannelModeParameterIds[bandIndex]);
    if (parameter == nullptr)
        return;

    const auto plainValue = static_cast<float>(mode);
    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost(parameter->convertTo0to1(plainValue));
    parameter->endChangeGesture();
}

void PluginProcessor::setScopVerticalZoomDecibels(const size_t bandIndex, const float decibels)
{
    if (bandIndex >= scopVerticalZoomParameterIds.size())
        return;

    auto* parameter = getActiveParameter(scopVerticalZoomParameterIds[bandIndex]);
    if (parameter == nullptr)
        return;

    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost(parameter->convertTo0to1(juce::jlimit(ana::scop::minimumVerticalZoomDecibels,
                                                                      ana::scop::maximumVerticalZoomDecibels,
                                                                      decibels)));
    parameter->endChangeGesture();
}

void PluginProcessor::setScopBandNormalized(const size_t bandIndex, const bool shouldNormalize)
{
    if (bandIndex >= scopNormalizeParameterIds.size())
        return;

    auto* parameter = getActiveParameter(scopNormalizeParameterIds[bandIndex]);
    if (parameter == nullptr)
        return;

    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost(shouldNormalize ? 1.0f : 0.0f);
    parameter->endChangeGesture();
}

void PluginProcessor::setScopLinkedNormalized(const bool shouldNormalize)
{
    if (auto* parameter = getActiveParameter(scopLinkedNormalizeParameterId))
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost(shouldNormalize ? 1.0f : 0.0f);
        parameter->endChangeGesture();
    }
}

bool PluginProcessor::areScopHorizontalZoomControlsVisible() const noexcept
{
    if (const auto* value = getRawParameterValue(scopHorizontalZoomControlsParameterId))
        return value->load(std::memory_order_relaxed) >= 0.5f;

    return ana::scop::defaultZoomControlsVisible;
}

bool PluginProcessor::areScopHorizontalReadoutsVisible() const noexcept
{
    if (const auto* value = getRawParameterValue(scopHorizontalReadoutsParameterId))
        return value->load(std::memory_order_relaxed) >= 0.5f;

    return true;
}

void PluginProcessor::requestScopClear()
{
    if(isOfflineMode()) return;
    auto* parameter=parameters.getParameter(scopClearParameterId);
    parameter->beginChangeGesture(); parameter->setValueNotifyingHost(1.0f); parameter->endChangeGesture();
}

int PluginProcessor::getCollapsedDockCount() const
{
    const auto* value=parameters.getRawParameterValue(dockCountParameterId);
    return value!=nullptr?juce::jlimit(1,16,juce::roundToInt(value->load(std::memory_order_relaxed))):6;
}
