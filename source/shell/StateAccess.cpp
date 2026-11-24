#include "Processor.h"
#include "LayoutWeights.h"
#include "shared/shell/SubmoduleParameters.h"
#include "shared/scop/Settings.h"
#include "shared/spec/Settings.h"
#include "shared/corr/Settings.h"
#include "shared/scop/NoteLengths.h"

using ana::processor_detail::normalizeLayoutWeights;

#include <algorithm>
#include <cmath>

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

ana::ScopChannelMode PluginProcessor::getScopOverallMonitorMode() const noexcept
{
    if (const auto* value = getRawParameterValue(scopOverallMonitorParameterId))
    {
        const auto index = juce::jlimit(0, static_cast<int>(ana::scopChannelModeCount) - 1,
                                        static_cast<int>(std::round(value->load(std::memory_order_relaxed))));
        return static_cast<ana::ScopChannelMode>(index);
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

bool PluginProcessor::isScopLinkedNormalized() const noexcept
{
    if (const auto* value = getRawParameterValue(scopLinkedNormalizeParameterId))
        return value->load(std::memory_order_relaxed) >= 0.5f;
    return false;
}

juce::Point<int> PluginProcessor::getLastEditorSize() const noexcept
{
    return { lastEditorWidth.load(std::memory_order_relaxed),
             lastEditorHeight.load(std::memory_order_relaxed) };
}

bool PluginProcessor::areScopBandsEqualHeight() const noexcept
{
    const auto* value = getRawParameterValue(scopEqualHeightParameterId);
    return value != nullptr && value->load(std::memory_order_relaxed) >= 0.5f;
}

void PluginProcessor::setScopBandsEqualHeight(bool enabled)
{
    auto* parameter=parameters.getParameter(scopEqualHeightParameterId);
    const auto value=enabled?1.0f:0.0f;
    if(!juce::approximatelyEqual(parameter->getValue(),value)) parameter->setValueNotifyingHost(value);
    if (enabled)
    {
        std::array<float,ana::dsp::LinkwitzRileyCrossover::numBands> weights;
        weights.fill(1.0f);
        setScopBandHeightWeights(weights);
    }
}

bool PluginProcessor::areLvlsPartsCentered() const noexcept
{
    return bool(parameters.state.getProperty("ui.lvls.parts-centered",true));
}

void PluginProcessor::setLvlsPartsCentered(bool enabled)
{
    parameters.state.setProperty("ui.lvls.parts-centered",enabled,nullptr);
    if (enabled) setLvlsSectionWeights({1.0f,1.0f,1.0f});
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
    const auto* value = getRawParameterValue(scopOverallFullParameterId);
    return value != nullptr && value->load(std::memory_order_relaxed) >= 0.5f;
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

bool PluginProcessor::isScreensaverEnabled() const noexcept
{
    const auto* screensaver = parameters.getRawParameterValue(screensaverParameterId);
    return screensaver != nullptr && screensaver->load(std::memory_order_relaxed) >= 0.5f;
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
