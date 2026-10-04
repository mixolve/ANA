#include "Processor.h"
#include "shared/shell/GraphColours.h"
#include "shared/spec/SpectrumProcessing.h"
#include "shared/spec/FrequencyScale.h"
#include "shared/scop/Settings.h"
#include "shared/spec/Settings.h"
#include "shared/corr/Settings.h"
#include "shared/scop/TimeScale.h"
#include "shared/shell/SubmoduleParameters.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <tuple>

namespace
{
juce::String formatFrequency(const float value)
{
    return juce::String::formatted("%08.2f", std::max(0.0f, value));
}

juce::String formatScopTime(const float milliseconds)
{
    return juce::String(juce::roundToInt(milliseconds));
}
}

juce::AudioProcessorValueTreeState::ParameterLayout PluginProcessor::createParameterLayout()
{
    ana::submodules::ParameterLayout layout;
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { analysisModeParameterId, 1 }, ana::submodules::parameterName(analysisModeParameterId),
        juce::StringArray { "REALTIME", "OFFLINE" }, 0,
        juce::AudioParameterChoiceAttributes().withAutomatable(false).withMeta(true)));
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { cleanViewParameterId, 1 }, ana::submodules::parameterName(cleanViewParameterId), false,
        juce::AudioParameterBoolAttributes().withAutomatable(false).withMeta(true)));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { moduleParameterId, 1 }, ana::submodules::parameterName(moduleParameterId),
        juce::StringArray { "SPEC", "CORR", "LVLS", "SCOP" }, 0,
        juce::AudioParameterChoiceAttributes().withAutomatable(false).withMeta(true)));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { specViewParameterId, 1 }, ana::submodules::parameterName(specViewParameterId),
        juce::StringArray { "FREQ", "MAP" }, 0,
        juce::AudioParameterChoiceAttributes().withAutomatable(false).withMeta(true)));
    layout.add(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID { lvlsLoudnessWidthParameterId, 1 }, ana::submodules::parameterName(lvlsLoudnessWidthParameterId),
        ana::lvls::minimumMeterWidth, ana::lvls::maximumMeterWidth, ana::lvls::defaultMeterWidth,
        juce::AudioParameterIntAttributes().withAutomatable(false).withMeta(true)));
    for (const auto& [id, defaultValue] : std::array {
             std::pair { lvlsLoudnessClearOnPlayParameterId, false },
             std::pair { lvlsLoudnessResetClickParameterId, true },
             std::pair { lvlsHistoryClearOnPlayParameterId, false },
             std::pair { lvlsHistoryResetClickParameterId, true } })
        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID { id, 1 }, ana::submodules::parameterName(id), defaultValue,
            juce::AudioParameterBoolAttributes().withAutomatable(false).withMeta(true)));
    for (const auto& [id, defaultValue] : std::array {
             std::pair { lvlsHistoryRangeLowParameterId, ana::lvls::defaultDisplayLowDecibels },
             std::pair { lvlsHistoryRangeHighParameterId, ana::lvls::defaultDisplayHighDecibels } })
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { id, 1 }, ana::submodules::parameterName(id),
            juce::NormalisableRange<float> { ana::lvls::minimumDisplayDecibels,
                                           ana::lvls::maximumDisplayDecibels,
                                           ana::lvls::displayDecibelStep }, defaultValue,
            juce::AudioParameterFloatAttributes().withAutomatable(false).withMeta(true)));
    juce::StringArray fftSizeChoices;
    for (const auto size : ana::fft::StereoFftStream::supportedFftSizes)
        fftSizeChoices.add(juce::String(size));

    juce::StringArray mapTimeOverlapChoices;
    for (int choice = 0; choice < ana::spec::SpecProcessor::mapTimeOverlapChoiceCount; ++choice)
    {
        const auto factor = ana::spec::SpecProcessor::mapTimeOversamplingFactor(choice);
        mapTimeOverlapChoices.add(factor == 1 ? juce::String("NONE")
                                              : juce::String(factor) + "x");
    }

    auto timeRange = juce::NormalisableRange<float> { ana::scop::minimumTimeMilliseconds,
                                                      ana::scop::maximumTimeMilliseconds,
                                                      ana::scop::timeStepMilliseconds };
    timeRange.setSkewForCentre(ana::scop::timeRangeSkewCentreMilliseconds);
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { scopTimeParameterId, 1 },
        ana::submodules::parameterName(scopTimeParameterId),
        timeRange,
        ana::scop::defaultTimeMilliseconds,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction([] (const float value, int)
            {
                return formatScopTime(value);
            })));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { scopTimeBaseParameterId, 1 },
        ana::submodules::parameterName(scopTimeBaseParameterId),
        juce::StringArray { "MS", "NOTE" },
        0,
        juce::AudioParameterChoiceAttributes().withAutomatable(false).withMeta(true)));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { scopNoteLengthParameterId, 1 },
        ana::submodules::parameterName(scopNoteLengthParameterId),
        []
        {
            juce::StringArray choices;
            for (const auto* label : ana::scop::noteLengthLabels)
                choices.add(label);
            return choices;
        }(),
        ana::scop::defaultNoteLengthIndex,
        juce::AudioParameterChoiceAttributes().withAutomatable(false).withMeta(true)));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { scopStyleParameterId, 1 },
        ana::submodules::parameterName(scopStyleParameterId),
        juce::StringArray { "FILLED", "OUTLINE" },
        ana::scop::defaultFilledStyle ? 0 : 1,
        juce::AudioParameterChoiceAttributes().withAutomatable(false).withMeta(true)));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { scopOpacityParameterId, 1 },
        ana::submodules::parameterName(scopOpacityParameterId),
        juce::NormalisableRange<float> { ana::scop::minimumOpacityPercent,
                                           ana::scop::maximumOpacityPercent,
                                           ana::scop::opacityStepPercent },
        ana::scop::defaultOpacityPercent,
        juce::AudioParameterFloatAttributes()
            .withAutomatable(false)
            .withMeta(true)
            .withStringFromValueFunction([] (const float value, int)
            {
                return juce::String(juce::roundToInt(value));
            })));

    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { scopVerticalZoomControlsParameterId, 1 },
        ana::submodules::parameterName(scopVerticalZoomControlsParameterId),
        ana::scop::defaultZoomControlsVisible,
        juce::AudioParameterBoolAttributes().withAutomatable(false).withMeta(true)));

    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { scopVerticalReadoutsParameterId, 1 },
        ana::submodules::parameterName(scopVerticalReadoutsParameterId), true,
        juce::AudioParameterBoolAttributes().withAutomatable(false).withMeta(true)));

    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { scopMonitorControlsParameterId, 1 },
        ana::submodules::parameterName(scopMonitorControlsParameterId),
        ana::scop::defaultMonitorControlsVisible,
        juce::AudioParameterBoolAttributes().withAutomatable(false).withMeta(true)));

    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { scopToolsParameterId, 1 },
        ana::submodules::parameterName(scopToolsParameterId),
        ana::scop::defaultToolsVisible,
        juce::AudioParameterBoolAttributes().withAutomatable(false).withMeta(true)));

    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { scopLeftToRightParameterId, 1 },
        ana::submodules::parameterName(scopLeftToRightParameterId),
        false,
        juce::AudioParameterBoolAttributes().withAutomatable(false).withMeta(true)));

    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { scopHorizontalZoomControlsParameterId, 1 },
        ana::submodules::parameterName(scopHorizontalZoomControlsParameterId),
        ana::scop::defaultZoomControlsVisible,
        juce::AudioParameterBoolAttributes().withAutomatable(false).withMeta(true)));

    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { scopHorizontalReadoutsParameterId, 1 },
        ana::submodules::parameterName(scopHorizontalReadoutsParameterId), true,
        juce::AudioParameterBoolAttributes().withAutomatable(false).withMeta(true)));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { specFftSizeParameterId, 1 },
        ana::submodules::parameterName(specFftSizeParameterId),
        fftSizeChoices,
        static_cast<int>(ana::fft::StereoFftStream::defaultFftSizeIndex),
        juce::AudioParameterChoiceAttributes().withAutomatable(false).withMeta(true)));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { specFftOverlapParameterId, 1 },
        ana::submodules::parameterName(specFftOverlapParameterId),
        juce::NormalisableRange<float> { ana::fft::StereoFftStream::minimumOverlap,
                                           ana::fft::StereoFftStream::maximumOverlap,
                                           ana::fft::StereoFftStream::overlapParameterStep },
        ana::fft::StereoFftStream::defaultOverlap,
        juce::AudioParameterFloatAttributes()
            .withAutomatable(false)
            .withMeta(true)
            .withStringFromValueFunction([] (const float value, int)
            {
                return juce::String(juce::roundToInt(value * 100.0f));
            })));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { specMapTimeOverlapParameterId, 1 },
        ana::submodules::parameterName(specMapTimeOverlapParameterId),
        mapTimeOverlapChoices,
        0,
        juce::AudioParameterChoiceAttributes().withAutomatable(false).withMeta(true)));

    auto mapTimeRange = juce::NormalisableRange<float> { ana::spec::minimumMapTimeMilliseconds,
                                                         ana::spec::maximumMapTimeMilliseconds,
                                                         ana::spec::mapTimeStepMilliseconds };
    mapTimeRange.setSkewForCentre(ana::spec::mapTimeRangeSkewCentreMilliseconds);
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { specMapTimeParameterId, 1 },
        ana::submodules::parameterName(specMapTimeParameterId),
        mapTimeRange,
        ana::spec::defaultMapTimeMilliseconds,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction([] (const float value, int)
            {
                return juce::String(juce::roundToInt(value));
            })));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { specMapTimeBaseParameterId, 1 },
        ana::submodules::parameterName(specMapTimeBaseParameterId),
        juce::StringArray { "MS", "NOTE" },
        0,
        juce::AudioParameterChoiceAttributes().withAutomatable(false).withMeta(true)));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { specMapNoteLengthParameterId, 1 },
        ana::submodules::parameterName(specMapNoteLengthParameterId),
        []
        {
            juce::StringArray choices;
            for (const auto* label : ana::scop::noteLengthLabels)
                choices.add(label);
            return choices;
        }(),
        ana::scop::defaultNoteLengthIndex,
        juce::AudioParameterChoiceAttributes().withAutomatable(false).withMeta(true)));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { specMapColourMapParameterId, 1 },
        ana::submodules::parameterName(specMapColourMapParameterId),
        []
        {
            juce::StringArray choices;
            for (const auto* label : ana::spec::colourMapLabels)
                choices.add(label);
            return choices;
        }(),
        ana::spec::defaultColourMapIndex,
        juce::AudioParameterChoiceAttributes().withAutomatable(false).withMeta(true)));

    for (const auto& [id, defaultValue] : std::array {
             std::tuple { specMapTimeRangeStartParameterId, 0.0f },
             std::tuple { specMapTimeRangeEndParameterId, 1.0f } })
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { id, 1 }, ana::submodules::parameterName(id),
            juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, defaultValue,
            juce::AudioParameterFloatAttributes().withAutomatable(false).withMeta(true)));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { specAverageTimeParameterId, 1 },
        ana::submodules::parameterName(specAverageTimeParameterId),
        juce::NormalisableRange<float> { ana::spectrum_processing::minimumAveragingTimeMilliseconds,
                                           ana::spectrum_processing::maximumAveragingTimeMilliseconds,
                                           ana::spectrum_processing::averagingTimeStepMilliseconds },
        ana::spectrum_processing::defaultAveragingTimeMilliseconds,
        juce::AudioParameterFloatAttributes().withAutomatable(false).withMeta(true)));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { specSmoothingParameterId, 1 },
        ana::submodules::parameterName(specSmoothingParameterId),
        juce::NormalisableRange<float> { ana::spectrum_processing::minimumSmoothingPercent,
                                           ana::spectrum_processing::maximumSmoothingPercent,
                                           ana::spectrum_processing::smoothingStepPercent },
        ana::spectrum_processing::defaultSmoothingPercent,
        juce::AudioParameterFloatAttributes().withAutomatable(false).withMeta(true)));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { specFrequencyScaleParameterId, 1 },
        ana::submodules::parameterName(specFrequencyScaleParameterId),
        juce::StringArray { "LIN", "EXT-LOG", "LOG", "MEL" },
        ana::frequency_scale::defaultScaleIndex,
        juce::AudioParameterChoiceAttributes().withAutomatable(false).withMeta(true)));

    for (const auto& [id, defaultValue] : std::array {
             std::tuple { specFilledDisplayParameterId, true },
             std::tuple { specSecondGraphParameterId, false },
             std::tuple { specAntiAliasParameterId, true },
             std::tuple { specHighQualityRenderingParameterId, true },
             std::tuple { specMapLeftToRightParameterId, false },
             std::tuple { specHorizontalReadoutsParameterId, true },
             std::tuple { specVerticalReadoutsParameterId, true },
             std::tuple { specClearOnPlayParameterId, false },
             std::tuple { specResetClickParameterId, true },
             std::tuple { specCursorReadoutParameterId, true },
             std::tuple { specCursorVerticalReadoutParameterId, true },
             std::tuple { specCursorNotesParameterId, true },
             std::tuple { specMonitorControlsParameterId, true },
             std::tuple { specHorizontalZoomParameterId, true },
             std::tuple { specVerticalZoomParameterId, true },
             std::tuple { specSplitViewParameterId, false } })
        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID { id, 1 }, ana::submodules::parameterName(id), defaultValue,
            juce::AudioParameterBoolAttributes().withAutomatable(false).withMeta(true)));

    juce::StringArray specMonitorModes;
    for (const auto* label : ana::spec::monitorModeLabels)
        specMonitorModes.add(juce::String::fromUTF8(label));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { specMonitorModeParameterId, 1 },
        ana::submodules::parameterName(specMonitorModeParameterId), specMonitorModes, 0,
        juce::AudioParameterChoiceAttributes().withAutomatable(false).withMeta(true)));

    for (const auto* id : std::array {
             specFirstGraphTypeParameterId,
             specSecondGraphTypeParameterId })
        layout.add(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID { id, 1 }, ana::submodules::parameterName(id),
            juce::StringArray { "AVG", "MAX" }, 0,
            juce::AudioParameterChoiceAttributes().withAutomatable(false).withMeta(true)));

    for (const auto& [id, defaultIndex] : std::array {
             std::tuple { specFirstGraphColourParameterId, ana::ui::defaultFirstGraphColourIndex },
             std::tuple { specSecondGraphColourParameterId, ana::ui::defaultSecondGraphColourIndex } })
        layout.add(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID { id, 1 }, ana::submodules::parameterName(id), ana::ui::graphColourNames(), defaultIndex,
            juce::AudioParameterChoiceAttributes().withAutomatable(false).withMeta(true)));

    layout.add(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID { specGraphOpacityParameterId, 1 },
        ana::submodules::parameterName(specGraphOpacityParameterId), 1, 100, 100,
        juce::AudioParameterIntAttributes().withAutomatable(false).withMeta(true)));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { specSlopeParameterId, 1 },
        ana::submodules::parameterName(specSlopeParameterId),
        juce::NormalisableRange<float> { ana::spec::minimumSlopeDecibelsPerOctave,
                                           ana::spec::maximumSlopeDecibelsPerOctave,
                                           ana::spec::slopeStepDecibelsPerOctave },
        ana::spec::defaultSlopeDecibelsPerOctave,
        juce::AudioParameterFloatAttributes().withAutomatable(false).withMeta(true)));

    const auto addRangeParameter = [&layout] (const char* id,
                                               const float minimum, const float maximum,
                                               const float defaultValue, const float step)
    {
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { id, 1 }, ana::submodules::parameterName(id),
            juce::NormalisableRange<float> { minimum, maximum, step }, defaultValue,
            juce::AudioParameterFloatAttributes().withAutomatable(false).withMeta(true)));
    };
    addRangeParameter(specLowParameterId, ana::frequency_scale::minimumHz, ana::frequency_scale::maximumHz, ana::frequency_scale::minimumHz, ana::frequency_scale::parameterStepHz);
    addRangeParameter(specHighParameterId, ana::frequency_scale::minimumHz, ana::frequency_scale::maximumHz, ana::frequency_scale::maximumHz, ana::frequency_scale::parameterStepHz);
    addRangeParameter(specRangeLowParameterId,
                      ana::spec::minimumDisplayDecibels,
                      ana::spec::maximumDisplayDecibels - ana::spec::minimumDisplaySpanDecibels,
                      ana::spec::defaultDisplayLowDecibels, ana::spec::displayRangeStepDecibels);
    addRangeParameter(specRangeHighParameterId,
                      ana::spec::minimumDisplayDecibels + ana::spec::minimumDisplaySpanDecibels,
                      ana::spec::maximumDisplayDecibels,
                      ana::spec::defaultDisplayHighDecibels, ana::spec::displayRangeStepDecibels);

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { corrFftSizeParameterId, 1 },
        ana::submodules::parameterName(corrFftSizeParameterId),
        fftSizeChoices,
        static_cast<int>(ana::fft::StereoFftStream::defaultFftSizeIndex),
        juce::AudioParameterChoiceAttributes().withAutomatable(false).withMeta(true)));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { corrFftOverlapParameterId, 1 },
        ana::submodules::parameterName(corrFftOverlapParameterId),
        juce::NormalisableRange<float> { ana::fft::StereoFftStream::minimumOverlap,
                                           ana::fft::StereoFftStream::maximumOverlap,
                                           ana::fft::StereoFftStream::overlapParameterStep },
        ana::fft::StereoFftStream::defaultOverlap,
        juce::AudioParameterFloatAttributes()
            .withAutomatable(false)
            .withMeta(true)
            .withStringFromValueFunction([] (const float value, int)
            {
                return juce::String(juce::roundToInt(value * 100.0f));
            })));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { corrSmoothingParameterId, 1 }, ana::submodules::parameterName(corrSmoothingParameterId),
        juce::NormalisableRange<float> { ana::spectrum_processing::minimumSmoothingPercent,
                                           ana::spectrum_processing::maximumSmoothingPercent,
                                           ana::spectrum_processing::smoothingStepPercent },
        ana::spectrum_processing::defaultSmoothingPercent,
        juce::AudioParameterFloatAttributes().withAutomatable(false).withMeta(true)));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { corrFrequencyScaleParameterId, 1 },
        ana::submodules::parameterName(corrFrequencyScaleParameterId),
        juce::StringArray { "LIN", "EXT-LOG", "LOG", "MEL" },
        ana::frequency_scale::defaultScaleIndex,
        juce::AudioParameterChoiceAttributes().withAutomatable(false).withMeta(true)));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { corrAverageTimeParameterId, 1 }, ana::submodules::parameterName(corrAverageTimeParameterId),
        juce::NormalisableRange<float> { ana::spectrum_processing::minimumAveragingTimeMilliseconds,
                                           ana::spectrum_processing::maximumAveragingTimeMilliseconds,
                                           ana::spectrum_processing::averagingTimeStepMilliseconds },
        ana::spectrum_processing::defaultAveragingTimeMilliseconds,
        juce::AudioParameterFloatAttributes().withAutomatable(false).withMeta(true)));

    for (const auto& [id, defaultValue] : std::array {
             std::tuple { corrFilledDisplayParameterId, true },
             std::tuple { corrSecondGraphParameterId, false },
             std::tuple { corrClearOnPlayParameterId, false },
             std::tuple { corrResetClickParameterId, true },
             std::tuple { corrHorizontalReadoutsParameterId, true },
             std::tuple { corrVerticalReadoutsParameterId, true },
             std::tuple { corrCursorNotesParameterId, true },
             std::tuple { corrCursorReadoutParameterId, true },
             std::tuple { corrCursorVerticalReadoutParameterId, true },
             std::tuple { corrHorizontalZoomParameterId, true },
             std::tuple { corrVerticalZoomParameterId, true } })
        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID { id, 1 }, ana::submodules::parameterName(id), defaultValue,
            juce::AudioParameterBoolAttributes().withAutomatable(false).withMeta(true)));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { corrFirstGraphTypeParameterId, 1 },
        ana::submodules::parameterName(corrFirstGraphTypeParameterId), juce::StringArray { "AVG", "MIN" }, 0,
        juce::AudioParameterChoiceAttributes().withAutomatable(false).withMeta(true)));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { corrSecondGraphTypeParameterId, 1 },
        ana::submodules::parameterName(corrSecondGraphTypeParameterId), juce::StringArray { "AVG", "MIN" }, 1,
        juce::AudioParameterChoiceAttributes().withAutomatable(false).withMeta(true)));

    for (const auto& [id, defaultIndex] : std::array {
             std::tuple { corrFirstGraphColourParameterId, ana::ui::defaultFirstGraphColourIndex },
             std::tuple { corrSecondGraphColourParameterId, ana::ui::defaultSecondGraphColourIndex } })
        layout.add(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID { id, 1 }, ana::submodules::parameterName(id), ana::ui::graphColourNames(), defaultIndex,
            juce::AudioParameterChoiceAttributes().withAutomatable(false).withMeta(true)));

    layout.add(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID { corrGraphOpacityParameterId, 1 },
        ana::submodules::parameterName(corrGraphOpacityParameterId), 1, 100, 100,
        juce::AudioParameterIntAttributes().withAutomatable(false).withMeta(true)));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { corrModeParameterId, 1 },
        ana::submodules::parameterName(corrModeParameterId), juce::StringArray { "PHASE", "FREQ", "SIGNED" }, 0,
        juce::AudioParameterChoiceAttributes().withAutomatable(false).withMeta(true)));
    addRangeParameter(corrLowParameterId, ana::frequency_scale::minimumHz, ana::frequency_scale::maximumHz, ana::frequency_scale::minimumHz, ana::frequency_scale::parameterStepHz);
    addRangeParameter(corrHighParameterId, ana::frequency_scale::minimumHz, ana::frequency_scale::maximumHz, ana::frequency_scale::maximumHz, ana::frequency_scale::parameterStepHz);
    addRangeParameter(corrRangeLowParameterId,
                      ana::corr::minimumCoefficient, ana::corr::maximumCoefficient,
                      ana::corr::defaultLowCoefficient, ana::corr::coefficientStep);
    addRangeParameter(corrRangeHighParameterId,
                      ana::corr::minimumCoefficient, ana::corr::maximumCoefficient,
                      ana::corr::defaultHighCoefficient, ana::corr::coefficientStep);

    layout.add(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID { lvlsWidthParameterId, 1 },
        ana::submodules::parameterName(lvlsWidthParameterId), ana::lvls::minimumMeterWidth, ana::lvls::maximumMeterWidth,
        ana::lvls::defaultMeterWidth,
        juce::AudioParameterIntAttributes().withAutomatable(false).withMeta(true)));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { lvlsPeakRangeHighParameterId, 1 }, ana::submodules::parameterName(lvlsPeakRangeHighParameterId),
        juce::NormalisableRange<float> { ana::lvls::minimumDisplayDecibels,
                                           ana::lvls::maximumDisplayDecibels,
                                           ana::lvls::displayDecibelStep },
        ana::lvls::defaultDisplayHighDecibels,
        juce::AudioParameterFloatAttributes().withAutomatable(false).withMeta(true)));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { lvlsPeakRangeLowParameterId, 1 }, ana::submodules::parameterName(lvlsPeakRangeLowParameterId),
        juce::NormalisableRange<float> { ana::lvls::minimumDisplayDecibels,
                                           ana::lvls::maximumDisplayDecibels,
                                           ana::lvls::displayDecibelStep },
        ana::lvls::defaultDisplayLowDecibels,
        juce::AudioParameterFloatAttributes().withAutomatable(false).withMeta(true)));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { lvlsLoudnessRangeHighParameterId, 1 }, ana::submodules::parameterName(lvlsLoudnessRangeHighParameterId),
        juce::NormalisableRange<float> { ana::lvls::minimumDisplayDecibels,
                                           ana::lvls::maximumDisplayDecibels,
                                           ana::lvls::displayDecibelStep },
        ana::lvls::defaultDisplayHighDecibels,
        juce::AudioParameterFloatAttributes().withAutomatable(false).withMeta(true)));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { lvlsLoudnessRangeLowParameterId, 1 }, ana::submodules::parameterName(lvlsLoudnessRangeLowParameterId),
        juce::NormalisableRange<float> { ana::lvls::minimumDisplayDecibels,
                                           ana::lvls::maximumDisplayDecibels,
                                           ana::lvls::displayDecibelStep },
        ana::lvls::defaultDisplayLowDecibels,
        juce::AudioParameterFloatAttributes().withAutomatable(false).withMeta(true)));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { lvlsRmsWindowMsParameterId, 1 },
        ana::submodules::parameterName(lvlsRmsWindowMsParameterId), juce::NormalisableRange<float> { ana::lvls::minimumRmsWindowMilliseconds,
                                               ana::lvls::maximumRmsWindowMilliseconds,
                                               ana::lvls::rmsWindowStepMilliseconds },
        ana::lvls::defaultRmsWindowMilliseconds,
        juce::AudioParameterFloatAttributes().withAutomatable(false).withMeta(true)));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { lvlsPeakHoldMsParameterId, 1 },
        ana::submodules::parameterName(lvlsPeakHoldMsParameterId), juce::NormalisableRange<float> { ana::lvls::minimumPeakHoldMilliseconds,
                                               ana::lvls::maximumPeakHoldMilliseconds,
                                               ana::lvls::peakHoldStepMilliseconds },
        ana::lvls::defaultPeakHoldMilliseconds,
        juce::AudioParameterFloatAttributes().withAutomatable(false).withMeta(true)));

    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { lvlsClearOnPlayParameterId, 1 }, ana::submodules::parameterName(lvlsClearOnPlayParameterId), false,
        juce::AudioParameterBoolAttributes().withAutomatable(false).withMeta(true)));
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { lvlsResetClickParameterId, 1 }, ana::submodules::parameterName(lvlsResetClickParameterId), true,
        juce::AudioParameterBoolAttributes().withAutomatable(false).withMeta(true)));

    for (const auto* id : std::array {
             lvlsPeakRmsVisibleParameterId,
             lvlsLoudnessVisibleParameterId,
             lvlsHistoryVisibleParameterId })
        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID { id, 1 }, ana::submodules::parameterName(id), true,
            juce::AudioParameterBoolAttributes().withAutomatable(false).withMeta(true)));

    for (const auto* id : std::array {
             lvlsHistoryMomentaryVisibleParameterId,
             lvlsHistoryShortTermVisibleParameterId,
             lvlsHistoryIntegratedVisibleParameterId,
             lvlsHistoryHorizontalZoomParameterId,
             lvlsHistoryVerticalZoomParameterId,
             lvlsHistoryHorizontalReadoutsParameterId,
             lvlsHistoryVerticalReadoutsParameterId })
        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID { id, 1 }, ana::submodules::parameterName(id), true,
            juce::AudioParameterBoolAttributes().withAutomatable(false).withMeta(true)));

    for (const auto& [id, defaultValue] : std::array {
             std::tuple { lvlsPeakModeParameterId, true },
             std::tuple { lvlsMidSideModeParameterId, false },
             std::tuple { lvlsHistorySoloParameterId, false } })
        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID { id, 1 }, ana::submodules::parameterName(id), defaultValue,
            juce::AudioParameterBoolAttributes().withAutomatable(false).withMeta(true)));

    for (const auto& [id, defaultValue] : std::array {
             std::tuple { lvlsHistoryHorizontalStartParameterId, 0.0f },
             std::tuple { lvlsHistoryHorizontalEndParameterId, 1.0f },
             std::tuple { lvlsHistoryVerticalStartParameterId, 0.0f },
             std::tuple { lvlsHistoryVerticalEndParameterId, 1.0f } })
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { id, 1 }, ana::submodules::parameterName(id),
            juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, defaultValue,
            juce::AudioParameterFloatAttributes().withAutomatable(false).withMeta(true)));

    const juce::StringArray scopChannelModes { "LEFT", "RIGHT", "MID", "SIDE", "LR", "MS" };
    for (size_t bandIndex = 0; bandIndex < scopChannelModeParameterIds.size(); ++bandIndex)
    {
        layout.add(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID { scopChannelModeParameterIds[bandIndex], 1 },
            ana::submodules::parameterName(scopChannelModeParameterIds[bandIndex]),
            scopChannelModes,
            static_cast<int>(ana::scop::defaultChannelMode),
            juce::AudioParameterChoiceAttributes().withAutomatable(false).withMeta(true)));

        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { scopVerticalZoomParameterIds[bandIndex], 1 },
            ana::submodules::parameterName(scopVerticalZoomParameterIds[bandIndex]),
            juce::NormalisableRange<float> { ana::scop::minimumVerticalZoomDecibels,
                                             ana::scop::maximumVerticalZoomDecibels,
                                             ana::scop::verticalZoomStepDecibels },
            ana::scop::defaultVerticalZoomDecibels,
            juce::AudioParameterFloatAttributes()
                .withAutomatable(false)
                .withMeta(true)
                .withStringFromValueFunction([] (const float value, int)
                {
                    return juce::String::formatted(
                        "%+.1f", std::abs(value) < 0.05f ? 0.0f : value);
                })));

        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID { scopNormalizeParameterIds[bandIndex], 1 },
            ana::submodules::parameterName(scopNormalizeParameterIds[bandIndex]),
            ana::scop::defaultBandNormalized,
            juce::AudioParameterBoolAttributes().withAutomatable(false).withMeta(true)));

        for (const auto& [id, defaultValue] : std::array {
                 std::tuple { scopRangeStartParameterIds[bandIndex], 0.0f },
                 std::tuple { scopRangeEndParameterIds[bandIndex], 1.0f } })
            layout.add(std::make_unique<juce::AudioParameterFloat>(
                juce::ParameterID { id, 1 }, ana::submodules::parameterName(id),
                juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, defaultValue,
                juce::AudioParameterFloatAttributes().withAutomatable(false).withMeta(true)));
    }

    layout.add(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID { crossoverCountParameterId, 1 },
        ana::submodules::parameterName(crossoverCountParameterId),
        0,
        static_cast<int>(ana::dsp::LinkwitzRileyCrossover::numCrossovers),
        static_cast<int>(ana::dsp::LinkwitzRileyCrossover::numCrossovers),
        juce::AudioParameterIntAttributes().withAutomatable(false).withMeta(true)));

    constexpr auto& defaults = ana::dsp::LinkwitzRileyCrossover::defaultFrequencies;

    for (size_t index = 0; index < defaults.size(); ++index)
    {
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { crossoverParameterIds[index], 1 },
            ana::submodules::parameterName(crossoverParameterIds[index]),
            juce::NormalisableRange<float> { ana::frequency_scale::minimumHz,
                                             ana::frequency_scale::maximumHz, ana::frequency_scale::parameterStepHz },
            static_cast<float>(defaults[index]),
            juce::AudioParameterFloatAttributes()
                .withAutomatable(false)
                .withMeta(true)
                .withStringFromValueFunction([] (const float value, int)
                {
                    return formatFrequency(value);
                })));
    }

    return layout.release();
}
