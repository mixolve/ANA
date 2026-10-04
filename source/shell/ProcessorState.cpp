#include "Processor.h"
#include "shared/shell/SubmoduleParameters.h"
#include "shared/scop/Settings.h"
#include "shared/spec/Settings.h"
#include "shared/corr/Settings.h"
#include "shared/scop/TimeScale.h"

#include <algorithm>
#include <cmath>

namespace
{
template <size_t Count>
void normalizeLayoutWeights(std::array<float, Count>& weights) noexcept
{
    auto total = 0.0;
    for (const auto weight : weights)
        total += static_cast<double>(weight);
    for (auto& weight : weights)
        weight = static_cast<float>(static_cast<double>(weight) / total);
}
}

double PluginProcessor::getScopTimeMilliseconds() const noexcept
{
    if (isScopTimeNoteBased())
    {
        const auto* noteValue = getRawParameterValue(scopNoteLengthParameterId);
        const auto noteIndex = noteValue != nullptr
            ? juce::roundToInt(noteValue->load(std::memory_order_relaxed))
            : ana::scop::defaultNoteLengthIndex;
        return ana::scop::noteLengthMilliseconds(
            noteIndex, hostTempoBpm.load(std::memory_order_relaxed));
    }

    if (const auto* value = getRawParameterValue(scopTimeParameterId))
        return static_cast<double>(value->load(std::memory_order_relaxed));

    return ana::scop::defaultTimeMilliseconds;
}

bool PluginProcessor::isScopTimeNoteBased() const noexcept
{
    if (const auto* value = getRawParameterValue(scopTimeBaseParameterId))
        return value->load(std::memory_order_relaxed) >= 0.5f;

    return false;
}

double PluginProcessor::getSpecMapTimeMilliseconds() const noexcept
{
    if (isSpecMapTimeNoteBased())
    {
        const auto* noteValue = getRawParameterValue(specMapNoteLengthParameterId);
        const auto noteIndex = noteValue != nullptr
            ? juce::roundToInt(noteValue->load(std::memory_order_relaxed))
            : ana::scop::defaultNoteLengthIndex;
        return ana::scop::noteLengthMilliseconds(
            noteIndex, hostTempoBpm.load(std::memory_order_relaxed));
    }

    if (const auto* value = getRawParameterValue(specMapTimeParameterId))
        return static_cast<double>(value->load(std::memory_order_relaxed));

    return ana::spec::defaultMapTimeMilliseconds;
}

bool PluginProcessor::isSpecMapTimeNoteBased() const noexcept
{
    if (const auto* value = getRawParameterValue(specMapTimeBaseParameterId))
        return value->load(std::memory_order_relaxed) >= 0.5f;

    return false;
}

bool PluginProcessor::isScopFilledStyle() const noexcept
{
    if (const auto* value = getRawParameterValue(scopStyleParameterId))
        return value->load(std::memory_order_relaxed) < 0.5f;

    return ana::scop::defaultFilledStyle;
}

float PluginProcessor::getScopOpacity() const noexcept
{
    if (const auto* value = getRawParameterValue(scopOpacityParameterId))
        return juce::jlimit(ana::scop::minimumOpacityPercent * 0.01f,
                            ana::scop::maximumOpacityPercent * 0.01f,
                            value->load(std::memory_order_relaxed) * 0.01f);

    return ana::scop::defaultOpacityPercent * 0.01f;
}

bool PluginProcessor::areScopVerticalZoomControlsVisible() const noexcept
{
    if (const auto* value = getRawParameterValue(scopVerticalZoomControlsParameterId))
        return value->load(std::memory_order_relaxed) >= 0.5f;

    return ana::scop::defaultZoomControlsVisible;
}

bool PluginProcessor::areScopVerticalReadoutsVisible() const noexcept
{
    if (const auto* value = getRawParameterValue(scopVerticalReadoutsParameterId))
        return value->load(std::memory_order_relaxed) >= 0.5f;

    return true;
}

bool PluginProcessor::areScopMonitorControlsVisible() const noexcept
{
    if (const auto* value = getRawParameterValue(scopMonitorControlsParameterId))
        return value->load(std::memory_order_relaxed) >= 0.5f;

    return ana::scop::defaultMonitorControlsVisible;
}

bool PluginProcessor::areScopToolsVisible() const noexcept
{
    if (const auto* value = getRawParameterValue(scopToolsParameterId))
        return value->load(std::memory_order_relaxed) >= 0.5f;

    return ana::scop::defaultToolsVisible;
}

size_t PluginProcessor::getCrossoverCount() const noexcept
{
    if (const auto* value = getRawParameterValue(crossoverCountParameterId))
    {
        const auto count = static_cast<int>(std::round(value->load(std::memory_order_relaxed)));
        return static_cast<size_t>(juce::jlimit(0, static_cast<int>(ana::dsp::LinkwitzRileyCrossover::numCrossovers), count));
    }

    return ana::dsp::LinkwitzRileyCrossover::numCrossovers;
}

ana::ScopChannelMode PluginProcessor::getScopChannelMode(const size_t bandIndex) const noexcept
{
    if (bandIndex < scopChannelModeParameterIds.size())
    {
        if (const auto* value = getRawParameterValue(scopChannelModeParameterIds[bandIndex]))
        {
            const auto mode = juce::jlimit(0, static_cast<int>(ana::scopChannelModeCount) - 1,
                                          static_cast<int>(std::round(value->load(std::memory_order_relaxed))));
            return static_cast<ana::ScopChannelMode>(mode);
        }
    }

    return ana::scop::defaultChannelMode;
}

float PluginProcessor::getScopVerticalZoomDecibels(const size_t bandIndex) const noexcept
{
    if (bandIndex < scopVerticalZoomParameterIds.size())
    {
        if (const auto* value = getRawParameterValue(scopVerticalZoomParameterIds[bandIndex]))
        {
            return juce::jlimit(ana::scop::minimumVerticalZoomDecibels,
                                ana::scop::maximumVerticalZoomDecibels,
                                value->load(std::memory_order_relaxed));
        }
    }

    return ana::scop::defaultVerticalZoomDecibels;
}

bool PluginProcessor::isScopBandNormalized(const size_t bandIndex) const noexcept
{
    if (bandIndex < scopNormalizeParameterIds.size())
        if (const auto* value = getRawParameterValue(scopNormalizeParameterIds[bandIndex]))
            return value->load(std::memory_order_relaxed) >= 0.5f;

    return ana::scop::defaultBandNormalized;
}


juce::Point<int> PluginProcessor::getLastEditorSize() const noexcept
{
    return { lastEditorWidth.load(std::memory_order_relaxed),
             lastEditorHeight.load(std::memory_order_relaxed) };
}

std::array<float, 3> PluginProcessor::getLvlsSectionWeights() const noexcept
{
    std::array<float, 3> weights {};

    for (size_t index = 0; index < weights.size(); ++index)
    {
        const auto stored = static_cast<float>(parameters.state.getProperty(
            lvlsSectionWeightStateKeys[index], 1.0));
        weights[index] = std::isfinite(stored) && stored > 0.0f ? stored : 1.0f;
    }

    normalizeLayoutWeights(weights);

    return weights;
}

std::array<float, ana::dsp::LinkwitzRileyCrossover::numBands>
PluginProcessor::getScopBandHeightWeights() const noexcept
{
    std::array<float, ana::dsp::LinkwitzRileyCrossover::numBands> weights {};
    for (size_t index = 0; index < weights.size(); ++index)
    {
        const auto stored = static_cast<float>(parameters.state.getProperty(
            scopBandHeightStateKeys[index], 1.0));
        weights[index] = std::isfinite(stored) && stored > 0.0f ? stored : 1.0f;
    }
    normalizeLayoutWeights(weights);
    return weights;
}

int PluginProcessor::getScopSingleViewBand() const noexcept
{
    return juce::jlimit(-1, static_cast<int>(ana::dsp::LinkwitzRileyCrossover::numBands) - 1,
                        static_cast<int>(parameters.state.getProperty(scopSingleViewStateKey, -1)));
}

bool PluginProcessor::isScopFullSourceView() const noexcept
{
    return static_cast<bool>(parameters.state.getProperty(scopFullSourceStateKey, false));
}

ana::AnalyzerPage PluginProcessor::getAnalyzerPageState() const noexcept
{
    const auto* module = parameters.getRawParameterValue(moduleParameterId);
    return ana::analyzerPageFromIndex(module != nullptr
        ? juce::roundToInt(module->load(std::memory_order_relaxed)) : 0);
}

bool PluginProcessor::isSpecMapView() const noexcept
{
    const auto* view = parameters.getRawParameterValue(specViewParameterId);
    return view != nullptr && view->load(std::memory_order_relaxed) >= 0.5f;
}

bool PluginProcessor::isCleanView() const noexcept
{
    const auto* clean = parameters.getRawParameterValue(cleanViewParameterId);
    return clean != nullptr && clean->load(std::memory_order_relaxed) >= 0.5f;
}


const char* PluginProcessor::resolveParameterId(const char* id) const noexcept
{
    const auto* mode = parameters.getRawParameterValue(corrModeParameterId);
    return ana::submodules::selectedId(id, isSpecMapView(),
        mode != nullptr ? juce::roundToInt(mode->load(std::memory_order_relaxed)) : 0);
}

std::atomic<float>* PluginProcessor::getRawParameterValue(const char* id) const noexcept
{
    return parameters.getRawParameterValue(resolveParameterId(id));
}

juce::RangedAudioParameter* PluginProcessor::getActiveParameter(const char* id) const noexcept
{
    return parameters.getParameter(resolveParameterId(id));
}

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
    updateHostDisplay(juce::AudioProcessorListener::ChangeDetails().withNonParameterStateChanged(true));
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
    corrProcessor.requestClear();
    updateHostDisplay(juce::AudioProcessorListener::ChangeDetails().withNonParameterStateChanged(true));
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

    lowParameter->setValueNotifyingHost(lowParameter->convertTo0to1(low));
    highParameter->setValueNotifyingHost(highParameter->convertTo0to1(high));
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
        updateHostDisplay(juce::AudioProcessorListener::ChangeDetails()
                              .withNonParameterStateChanged(true));
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
    auto changed = false;
    for (size_t index = 0; index < safeWeights.size(); ++index)
    {
        if (std::abs(current[index] - safeWeights[index]) <= 1.0e-5f)
            continue;

        parameters.state.setProperty(lvlsSectionWeightStateKeys[index], safeWeights[index], nullptr);
        changed = true;
    }

    if (changed)
        updateHostDisplay(juce::AudioProcessorListener::ChangeDetails()
                              .withNonParameterStateChanged(true));
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
    auto changed = false;
    for (size_t index = 0; index < safeWeights.size(); ++index)
    {
        if (std::abs(current[index] - safeWeights[index]) <= 1.0e-5f)
            continue;
        parameters.state.setProperty(scopBandHeightStateKeys[index], safeWeights[index], nullptr);
        changed = true;
    }
    if (changed)
        updateHostDisplay(juce::AudioProcessorListener::ChangeDetails()
                              .withNonParameterStateChanged(true));
}

void PluginProcessor::setAnalyzerPageState(const ana::AnalyzerPage page)
{
    if (getAnalyzerPageState() == page)
        return;

    if (auto* module = parameters.getParameter(moduleParameterId))
        module->setValueNotifyingHost(module->convertTo0to1(static_cast<float>(page)));
    if (page == ana::AnalyzerPage::spec)
        specProcessor.requestRealtimeReset();
    updateHostDisplay(juce::AudioProcessorListener::ChangeDetails().withNonParameterStateChanged(true));
}

void PluginProcessor::setSpecMapView(const bool shouldUseMap)
{
    auto* view = parameters.getParameter(specViewParameterId);
    if (view == nullptr || isSpecMapView() == shouldUseMap)
        return;
    view->setValueNotifyingHost(view->convertTo0to1(shouldUseMap ? 1.0f : 0.0f));
    updateHostDisplay(juce::AudioProcessorListener::ChangeDetails().withNonParameterStateChanged(true));
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
    updateHostDisplay(juce::AudioProcessorListener::ChangeDetails()
                          .withNonParameterStateChanged(true));
}

void PluginProcessor::setScopFullSourceView(const bool shouldShowFullSource)
{
    if (isScopFullSourceView() == shouldShowFullSource)
        return;

    parameters.state.setProperty(scopFullSourceStateKey, shouldShowFullSource, nullptr);
    if (shouldShowFullSource)
        parameters.state.setProperty(scopSingleViewStateKey, -1, nullptr);

    updateHostDisplay(juce::AudioProcessorListener::ChangeDetails()
                          .withNonParameterStateChanged(true));
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
