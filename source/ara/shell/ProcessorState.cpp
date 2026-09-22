#include "Processor.h"
#include "shared/scop/Settings.h"
#include "shared/spec/Settings.h"
#include "shared/corr/Settings.h"
#include "shared/scop/TimeScale.h"

#include <algorithm>
#include <cmath>

double PluginProcessor::getScopTimeMilliseconds() const noexcept
{
    if (isScopTimeNoteBased())
    {
        const auto* noteValue = parameters.getRawParameterValue(scopNoteLengthParameterId);
        const auto noteIndex = noteValue != nullptr
            ? juce::roundToInt(noteValue->load(std::memory_order_relaxed))
            : ana::scop::defaultNoteLengthIndex;
        return ana::scop::noteLengthMilliseconds(
            noteIndex, hostTempoBpm.load(std::memory_order_relaxed));
    }

    if (const auto* value = parameters.getRawParameterValue(scopTimeParameterId))
        return static_cast<double>(value->load(std::memory_order_relaxed));

    return ana::scop::defaultTimeMilliseconds;
}

bool PluginProcessor::isScopTimeNoteBased() const noexcept
{
    if (const auto* value = parameters.getRawParameterValue(scopTimeBaseParameterId))
        return value->load(std::memory_order_relaxed) >= 0.5f;

    return false;
}

double PluginProcessor::getSpecMapTimeMilliseconds() const noexcept
{
    if (isSpecMapTimeNoteBased())
    {
        const auto* noteValue = parameters.getRawParameterValue(specMapNoteLengthParameterId);
        const auto noteIndex = noteValue != nullptr
            ? juce::roundToInt(noteValue->load(std::memory_order_relaxed))
            : ana::scop::defaultNoteLengthIndex;
        return ana::scop::noteLengthMilliseconds(
            noteIndex, hostTempoBpm.load(std::memory_order_relaxed));
    }

    if (const auto* value = parameters.getRawParameterValue(specMapTimeParameterId))
        return static_cast<double>(value->load(std::memory_order_relaxed));

    return ana::spec::defaultMapTimeMilliseconds;
}

bool PluginProcessor::isSpecMapTimeNoteBased() const noexcept
{
    if (const auto* value = parameters.getRawParameterValue(specMapTimeBaseParameterId))
        return value->load(std::memory_order_relaxed) >= 0.5f;

    return false;
}

bool PluginProcessor::isScopFilledStyle() const noexcept
{
    if (const auto* value = parameters.getRawParameterValue(scopStyleParameterId))
        return value->load(std::memory_order_relaxed) < 0.5f;

    return ana::scop::defaultFilledStyle;
}

float PluginProcessor::getScopOpacity() const noexcept
{
    if (const auto* value = parameters.getRawParameterValue(scopOpacityParameterId))
        return juce::jlimit(ana::scop::minimumOpacityPercent * 0.01f,
                            ana::scop::maximumOpacityPercent * 0.01f,
                            value->load(std::memory_order_relaxed) * 0.01f);

    return ana::scop::defaultOpacityPercent * 0.01f;
}

bool PluginProcessor::areScopZoomControlsVisible() const noexcept
{
    if (const auto* value = parameters.getRawParameterValue(scopZoomControlsParameterId))
        return value->load(std::memory_order_relaxed) >= 0.5f;

    return ana::scop::defaultZoomControlsVisible;
}

bool PluginProcessor::areScopMonitorControlsVisible() const noexcept
{
    if (const auto* value = parameters.getRawParameterValue(scopMonitorControlsParameterId))
        return value->load(std::memory_order_relaxed) >= 0.5f;

    return ana::scop::defaultMonitorControlsVisible;
}

bool PluginProcessor::areScopToolsVisible() const noexcept
{
    if (const auto* value = parameters.getRawParameterValue(scopToolsParameterId))
        return value->load(std::memory_order_relaxed) >= 0.5f;

    return ana::scop::defaultToolsVisible;
}

size_t PluginProcessor::getCrossoverCount() const noexcept
{
    if (const auto* value = parameters.getRawParameterValue(crossoverCountParameterId))
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
        if (const auto* value = parameters.getRawParameterValue(scopChannelModeParameterIds[bandIndex]))
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
        if (const auto* value = parameters.getRawParameterValue(scopVerticalZoomParameterIds[bandIndex]))
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
        if (const auto* value = parameters.getRawParameterValue(scopNormalizeParameterIds[bandIndex]))
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
    auto total = 0.0f;

    for (size_t index = 0; index < weights.size(); ++index)
    {
        const auto stored = static_cast<float>(parameters.state.getProperty(
            lvlsSectionWeightStateKeys[index], 1.0));
        weights[index] = std::isfinite(stored) && stored > 0.0f ? stored : 1.0f;
        total += weights[index];
    }

    if (total <= 0.0f)
        return { 1.0f / 3.0f, 1.0f / 3.0f, 1.0f / 3.0f };

    for (auto& weight : weights)
        weight /= total;

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
    return activeAnalyzerPage.load(std::memory_order_acquire);
}

bool PluginProcessor::isSpecMapView() const noexcept
{
    return static_cast<int>(parameters.state.getProperty(specViewModeStateKey, 0)) != 0;
}


void PluginProcessor::setSpecMonitorMode(const int mode)
{
    const auto nextMode = juce::jlimit(
        0, static_cast<int>(ana::spec::monitorModeCount) - 1, mode);
    auto* modeParameter = parameters.getParameter(specMonitorModeParameterId);
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
    auto* modeParameter = parameters.getParameter(corrModeParameterId);
    if (modeParameter == nullptr)
        return;

    const auto previousMode = juce::roundToInt(modeParameter->getValue()
                                               * static_cast<float>(modeParameter->getNumSteps() - 1));
    if (previousMode == nextMode)
        return;

    static constexpr std::array settings {
        corrFftSizeParameterId, corrFftOverlapParameterId,
        corrAverageTimeParameterId, corrSmoothingParameterId,
        corrFilledDisplayParameterId, corrSecondGraphParameterId,
        corrFirstGraphTypeParameterId, corrSecondGraphTypeParameterId,
        corrFirstGraphColourParameterId, corrSecondGraphColourParameterId,
        corrGraphOpacityParameterId,
        corrClearOnPlayParameterId, corrRangesVisibleParameterId,
        corrCursorReadoutParameterId, corrZoomControlsParameterId,
        corrLowParameterId, corrHighParameterId,
        corrRangeLowParameterId, corrRangeHighParameterId
    };
    for (const auto* parameterId : settings)
    {
        auto* parameter = parameters.getParameter(parameterId);
        if (parameter == nullptr)
            continue;

        const auto previousKey = juce::Identifier(
            "corrMode" + juce::String(previousMode) + "_" + parameterId);
        parameters.state.setProperty(previousKey, parameter->getValue(), nullptr);

        const auto nextKey = juce::Identifier(
            "corrMode" + juce::String(nextMode) + "_" + parameterId);
        const auto nextValue = parameters.state.hasProperty(nextKey)
            ? static_cast<float>(parameters.state.getProperty(nextKey))
            : parameter->getDefaultValue();
        parameter->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, nextValue));
    }
    constrainCorrRangeForMode(nextMode);
    modeParameter->setValueNotifyingHost(modeParameter->convertTo0to1(static_cast<float>(nextMode)));
    corrProcessor.requestClear();
    updateHostDisplay(juce::AudioProcessorListener::ChangeDetails().withNonParameterStateChanged(true));
}

void PluginProcessor::constrainCorrRangeForMode(const int mode)
{
    if (mode != ana::corr::CorrProcessor::modeIndex(
                    ana::corr::CorrProcessor::Mode::frequency))
        return;

    auto* lowParameter = parameters.getParameter(corrRangeLowParameterId);
    auto* highParameter = parameters.getParameter(corrRangeHighParameterId);
    const auto* lowValue = parameters.getRawParameterValue(corrRangeLowParameterId);
    const auto* highValue = parameters.getRawParameterValue(corrRangeHighParameterId);
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
    auto total = 0.0f;
    for (auto& weight : safeWeights)
    {
        weight = std::isfinite(weight) ? std::max(0.001f, weight) : 1.0f;
        total += weight;
    }

    for (auto& weight : safeWeights)
        weight /= total;

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

void PluginProcessor::setAnalyzerPageState(const ana::AnalyzerPage page)
{
    if (getAnalyzerPageState() == page)
        return;

    activeAnalyzerPage.store(page, std::memory_order_release);
    parameters.state.setProperty(analyzerPageStateKey, static_cast<int>(page), nullptr);
    updateHostDisplay(juce::AudioProcessorListener::ChangeDetails().withNonParameterStateChanged(true));
}

void PluginProcessor::setSpecMapView(const bool shouldUseMap)
{
    const auto storedValue = static_cast<int>(parameters.state.getProperty(specViewModeStateKey, 0)) != 0;
    if (storedValue == shouldUseMap)
        return;

    parameters.state.setProperty(specViewModeStateKey, shouldUseMap ? 1 : 0, nullptr);
    updateHostDisplay(juce::AudioProcessorListener::ChangeDetails().withNonParameterStateChanged(true));
}

ana::dsp::LinkwitzRileyCrossover::CrossoverFrequencies PluginProcessor::getCrossoverFrequencies() const noexcept
{
    auto frequencies = ana::dsp::LinkwitzRileyCrossover::defaultFrequencies;

    for (size_t index = 0; index < frequencies.size(); ++index)
    {
        if (const auto* value = parameters.getRawParameterValue(crossoverParameterIds[index]))
            frequencies[index] = static_cast<double>(value->load(std::memory_order_relaxed));
    }

    return frequencies;
}

void PluginProcessor::setCrossoverCount(const size_t crossoverCount)
{
    auto* parameter = parameters.getParameter(crossoverCountParameterId);
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

    auto* parameter = parameters.getParameter(scopChannelModeParameterIds[bandIndex]);
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

    auto* parameter = parameters.getParameter(scopVerticalZoomParameterIds[bandIndex]);
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

    auto* parameter = parameters.getParameter(scopNormalizeParameterIds[bandIndex]);
    if (parameter == nullptr)
        return;

    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost(shouldNormalize ? 1.0f : 0.0f);
    parameter->endChangeGesture();
}
