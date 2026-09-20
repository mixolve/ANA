#include "Processor.h"
#include "shared/analyzer/DisplaySettings.h"
#include "shared/analyzer/Frequency.h"
#include "shared/scop/Settings.h"
#include "shared/spec/Settings.h"
#include "shared/corr/Settings.h"
#include "shared/scop/TimeScale.h"

#include <array>
#include <cmath>
#include <tuple>

namespace
{
juce::String formatFrequency(const float value)
{
    return juce::String(value, value >= 100.0f ? 0 : 1);
}

juce::String formatScopTime(const float milliseconds)
{
    const auto seconds = milliseconds / 1000.0f;
    return juce::String(seconds, seconds < 10.0f ? 1 : 0);
}
}

juce::AudioProcessorValueTreeState::ParameterLayout PluginProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
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
        "SCOP / TIME",
        timeRange,
        ana::scop::defaultTimeMilliseconds,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction([] (const float value, int)
            {
                return formatScopTime(value);
            })));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { scopTimeBaseParameterId, 1 },
        "SCOP / TIME BASE",
        juce::StringArray { "MS", "NOTE" },
        0,
        juce::AudioParameterChoiceAttributes().withAutomatable(false).withMeta(true)));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { scopNoteLengthParameterId, 1 },
        "SCOP / NOTE LENGTH",
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
        "SCOP / STYLE",
        juce::StringArray { "FILLED", "OUTLINE" },
        ana::scop::defaultFilledStyle ? 0 : 1,
        juce::AudioParameterChoiceAttributes().withAutomatable(false).withMeta(true)));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { scopOpacityParameterId, 1 },
        "SCOP / OPACITY",
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
        juce::ParameterID { scopZoomControlsParameterId, 1 },
        "SCOP / ZOOM",
        ana::scop::defaultZoomControlsVisible,
        juce::AudioParameterBoolAttributes().withAutomatable(false).withMeta(true)));

    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { scopMonitorControlsParameterId, 1 },
        "SCOP / MONITOR",
        ana::scop::defaultMonitorControlsVisible,
        juce::AudioParameterBoolAttributes().withAutomatable(false).withMeta(true)));

    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { scopToolsParameterId, 1 },
        "SCOP / TOOLS",
        ana::scop::defaultToolsVisible,
        juce::AudioParameterBoolAttributes().withAutomatable(false).withMeta(true)));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { specFftSizeParameterId, 1 },
        "SPEC / FFT SIZE",
        fftSizeChoices,
        static_cast<int>(ana::fft::StereoFftStream::defaultFftSizeIndex),
        juce::AudioParameterChoiceAttributes().withAutomatable(false).withMeta(true)));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { specFftOverlapParameterId, 1 },
        "SPEC / FFT OVERLAP",
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
        "SPEC MAP / TIME OVERLAP",
        mapTimeOverlapChoices,
        0,
        juce::AudioParameterChoiceAttributes().withAutomatable(false).withMeta(true)));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { specAverageTimeParameterId, 1 },
        "SPEC / AVG TIME",
        juce::NormalisableRange<float> { ana::analyzer_display::minimumAveragingTimeMilliseconds,
                                           ana::analyzer_display::maximumAveragingTimeMilliseconds,
                                           ana::analyzer_display::averagingTimeStepMilliseconds },
        ana::analyzer_display::defaultAveragingTimeMilliseconds,
        juce::AudioParameterFloatAttributes().withAutomatable(false).withMeta(true)));


    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { specSmoothingParameterId, 1 },
        "SPEC / SMOOTHING",
        juce::NormalisableRange<float> { ana::analyzer_display::minimumSmoothingPercent,
                                           ana::analyzer_display::maximumSmoothingPercent,
                                           ana::analyzer_display::smoothingStepPercent },
        ana::analyzer_display::defaultSmoothingPercent,
        juce::AudioParameterFloatAttributes().withAutomatable(false).withMeta(true)));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { specFrequencyScaleParameterId, 1 },
        "SPEC / FREQ SCALE",
        juce::StringArray { "LIN", "EXT-LOG", "LOG", "MEL" },
        ana::analyzer_frequency::defaultScaleIndex,
        juce::AudioParameterChoiceAttributes().withAutomatable(false).withMeta(true)));

    for (const auto& [id, name, defaultValue] : std::array {
             std::tuple { specFilledDisplayParameterId, "SPEC / FILLED DISPLAY", true },
             std::tuple { specSecondGraphParameterId, "SPEC / 2ND GRAPH", false },
             std::tuple { specAntiAliasParameterId, "SPEC / ANTI ALIAS", true },
             std::tuple { specHighQualityRenderingParameterId, "SPEC MAP / HIGH QUALITY RENDERING", true },
             std::tuple { specMapLeftToRightParameterId, "SPEC MAP / LEFT TO RIGHT", false },
             std::tuple { specRangesVisibleParameterId, "SPEC / RANGES", true },
             std::tuple { specClearOnPlayParameterId, "SPEC / CLEAR ON PLAY", false },
             std::tuple { specCursorReadoutParameterId, "SPEC / CURSOR", true },
             std::tuple { specMonitorControlsParameterId, "SPEC / MONITOR", true },
             std::tuple { specZoomControlsParameterId, "SPEC / ZOOM", true },
             std::tuple { specSplitViewParameterId, "SPEC / SPLIT", false } })
        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID { id, 1 }, name, defaultValue,
            juce::AudioParameterBoolAttributes().withAutomatable(false).withMeta(true)));

    juce::StringArray specMonitorModes;
    for (const auto* label : ana::spec::monitorModeLabels)
        specMonitorModes.add(juce::String::fromUTF8(label));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { specMonitorModeParameterId, 1 },
        "SPEC / MONITOR MODE", specMonitorModes, 0,
        juce::AudioParameterChoiceAttributes().withAutomatable(false).withMeta(true)));


    for (const auto& [id, name] : std::array {
             std::pair { specFirstGraphTypeParameterId, "SPEC / 1ST GRAPH TYPE" },
             std::pair { specSecondGraphTypeParameterId, "SPEC / 2ND GRAPH TYPE" } })
        layout.add(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID { id, 1 }, name,
            juce::StringArray { "AVG", "MAX" }, 0,
            juce::AudioParameterChoiceAttributes().withAutomatable(false).withMeta(true)));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { specSlopeParameterId, 1 },
        "SPEC / SLOPE",
        juce::NormalisableRange<float> { ana::spec::minimumSlopeDecibelsPerOctave,
                                           ana::spec::maximumSlopeDecibelsPerOctave,
                                           ana::spec::slopeStepDecibelsPerOctave },
        ana::spec::defaultSlopeDecibelsPerOctave,
        juce::AudioParameterFloatAttributes().withAutomatable(false).withMeta(true)));

    const auto addRangeParameter = [&layout] (const char* id, const juce::String& name,
                                               const float minimum, const float maximum,
                                               const float defaultValue, const float step)
    {
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { id, 1 }, name,
            juce::NormalisableRange<float> { minimum, maximum, step }, defaultValue,
            juce::AudioParameterFloatAttributes().withAutomatable(false).withMeta(true)));
    };
    addRangeParameter(specLowParameterId, "SPEC / LOW", ana::analyzer_frequency::minimumHz, ana::analyzer_frequency::maximumHz, ana::analyzer_frequency::minimumHz, ana::analyzer_frequency::parameterStepHz);
    addRangeParameter(specHighParameterId, "SPEC / HIGH", ana::analyzer_frequency::minimumHz, ana::analyzer_frequency::maximumHz, ana::analyzer_frequency::maximumHz, ana::analyzer_frequency::parameterStepHz);
    addRangeParameter(specRangeLowParameterId, "SPEC / RANGE LOW",
                      ana::spec::minimumDisplayDecibels,
                      ana::spec::maximumDisplayDecibels - ana::spec::minimumDisplaySpanDecibels,
                      ana::spec::defaultDisplayLowDecibels, ana::spec::displayRangeStepDecibels);
    addRangeParameter(specRangeHighParameterId, "SPEC / RANGE HIGH",
                      ana::spec::minimumDisplayDecibels + ana::spec::minimumDisplaySpanDecibels,
                      ana::spec::maximumDisplayDecibels,
                      ana::spec::defaultDisplayHighDecibels, ana::spec::displayRangeStepDecibels);

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { corrFftSizeParameterId, 1 },
        "CORR / FFT SIZE",
        fftSizeChoices,
        static_cast<int>(ana::fft::StereoFftStream::defaultFftSizeIndex),
        juce::AudioParameterChoiceAttributes().withAutomatable(false).withMeta(true)));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { corrFftOverlapParameterId, 1 },
        "CORR / FFT OVERLAP",
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
        juce::ParameterID { corrSmoothingParameterId, 1 }, "CORR / SMOOTHING",
        juce::NormalisableRange<float> { ana::analyzer_display::minimumSmoothingPercent,
                                           ana::analyzer_display::maximumSmoothingPercent,
                                           ana::analyzer_display::smoothingStepPercent },
        ana::analyzer_display::defaultSmoothingPercent,
        juce::AudioParameterFloatAttributes().withAutomatable(false).withMeta(true)));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { corrFrequencyScaleParameterId, 1 },
        "CORR / FREQ SCALE",
        juce::StringArray { "LIN", "EXT-LOG", "LOG", "MEL" },
        ana::analyzer_frequency::defaultScaleIndex,
        juce::AudioParameterChoiceAttributes().withAutomatable(false).withMeta(true)));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { corrAverageTimeParameterId, 1 }, "CORR / AVG TIME",
        juce::NormalisableRange<float> { ana::analyzer_display::minimumAveragingTimeMilliseconds,
                                           ana::analyzer_display::maximumAveragingTimeMilliseconds,
                                           ana::analyzer_display::averagingTimeStepMilliseconds },
        ana::analyzer_display::defaultAveragingTimeMilliseconds,
        juce::AudioParameterFloatAttributes().withAutomatable(false).withMeta(true)));

    for (const auto& [id, name, defaultValue] : std::array {
             std::tuple { corrFilledDisplayParameterId, "CORR / FILLED DISPLAY", true },
             std::tuple { corrSecondGraphParameterId, "CORR / 2ND GRAPH", false },
             std::tuple { corrClearOnPlayParameterId, "CORR / CLEAR ON PLAY", false },
             std::tuple { corrRangesVisibleParameterId, "CORR / RANGES", true },
             std::tuple { corrCursorReadoutParameterId, "CORR / CURSOR", true },
             std::tuple { corrZoomControlsParameterId, "CORR / ZOOM", true } })
        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID { id, 1 }, name, defaultValue,
            juce::AudioParameterBoolAttributes().withAutomatable(false).withMeta(true)));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { corrFirstGraphTypeParameterId, 1 },
        "CORR / 1ST GRAPH TYPE", juce::StringArray { "AVG", "MIN" }, 0,
        juce::AudioParameterChoiceAttributes().withAutomatable(false).withMeta(true)));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { corrSecondGraphTypeParameterId, 1 },
        "CORR / 2ND GRAPH TYPE", juce::StringArray { "AVG", "MIN" }, 1,
        juce::AudioParameterChoiceAttributes().withAutomatable(false).withMeta(true)));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { corrModeParameterId, 1 },
        "CORR / MODE", juce::StringArray { "PHASE", "FREQ", "SIGNED" }, 0,
        juce::AudioParameterChoiceAttributes().withAutomatable(false).withMeta(true)));
    addRangeParameter(corrLowParameterId, "CORR / LOW", ana::analyzer_frequency::minimumHz, ana::analyzer_frequency::maximumHz, ana::analyzer_frequency::minimumHz, ana::analyzer_frequency::parameterStepHz);
    addRangeParameter(corrHighParameterId, "CORR / HIGH", ana::analyzer_frequency::minimumHz, ana::analyzer_frequency::maximumHz, ana::analyzer_frequency::maximumHz, ana::analyzer_frequency::parameterStepHz);
    addRangeParameter(corrRangeLowParameterId, "CORR / RANGE LOW",
                      ana::corr::minimumCoefficient, ana::corr::maximumCoefficient,
                      ana::corr::defaultLowCoefficient, ana::corr::coefficientStep);
    addRangeParameter(corrRangeHighParameterId, "CORR / RANGE HIGH",
                      ana::corr::minimumCoefficient, ana::corr::maximumCoefficient,
                      ana::corr::defaultHighCoefficient, ana::corr::coefficientStep);

    layout.add(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID { lvlsWidthParameterId, 1 },
        "LVLS / WIDTH", ana::lvls::minimumMeterWidth, ana::lvls::maximumMeterWidth,
        ana::lvls::defaultMeterWidth,
        juce::AudioParameterIntAttributes().withAutomatable(false).withMeta(true)));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { lvlsPeakRangeHighParameterId, 1 }, "LVLS / PEAK/RMS HIGH",
        juce::NormalisableRange<float> { ana::lvls::minimumDisplayDecibels,
                                           ana::lvls::maximumDisplayDecibels,
                                           ana::lvls::displayDecibelStep },
        ana::lvls::defaultDisplayHighDecibels,
        juce::AudioParameterFloatAttributes().withAutomatable(false).withMeta(true)));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { lvlsPeakRangeLowParameterId, 1 }, "LVLS / PEAK/RMS LOW",
        juce::NormalisableRange<float> { ana::lvls::minimumDisplayDecibels,
                                           ana::lvls::maximumDisplayDecibels,
                                           ana::lvls::displayDecibelStep },
        ana::lvls::defaultDisplayLowDecibels,
        juce::AudioParameterFloatAttributes().withAutomatable(false).withMeta(true)));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { lvlsLoudnessRangeHighParameterId, 1 }, "LVLS / LUFS HIGH",
        juce::NormalisableRange<float> { ana::lvls::minimumDisplayDecibels,
                                           ana::lvls::maximumDisplayDecibels,
                                           ana::lvls::displayDecibelStep },
        ana::lvls::defaultDisplayHighDecibels,
        juce::AudioParameterFloatAttributes().withAutomatable(false).withMeta(true)));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { lvlsLoudnessRangeLowParameterId, 1 }, "LVLS / LUFS LOW",
        juce::NormalisableRange<float> { ana::lvls::minimumDisplayDecibels,
                                           ana::lvls::maximumDisplayDecibels,
                                           ana::lvls::displayDecibelStep },
        ana::lvls::defaultDisplayLowDecibels,
        juce::AudioParameterFloatAttributes().withAutomatable(false).withMeta(true)));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { lvlsRmsWindowMsParameterId, 1 },
        "LVLS / RMS WINDOW", juce::NormalisableRange<float> { ana::lvls::minimumRmsWindowMilliseconds,
                                               ana::lvls::maximumRmsWindowMilliseconds,
                                               ana::lvls::rmsWindowStepMilliseconds },
        ana::lvls::defaultRmsWindowMilliseconds,
        juce::AudioParameterFloatAttributes().withAutomatable(false).withMeta(true)));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { lvlsPeakHoldMsParameterId, 1 },
        "LVLS / PEAK HOLD", juce::NormalisableRange<float> { ana::lvls::minimumPeakHoldMilliseconds,
                                               ana::lvls::maximumPeakHoldMilliseconds,
                                               ana::lvls::peakHoldStepMilliseconds },
        ana::lvls::defaultPeakHoldMilliseconds,
        juce::AudioParameterFloatAttributes().withAutomatable(false).withMeta(true)));

    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { lvlsClearOnPlayParameterId, 1 }, "LVLS / CLEAR ON PLAY", false,
        juce::AudioParameterBoolAttributes().withAutomatable(false).withMeta(true)));

    for (const auto& [id, name] : std::array {
             std::pair { lvlsPeakRmsVisibleParameterId, "LVLS / PEAK RMS VISIBLE" },
             std::pair { lvlsLoudnessVisibleParameterId, "LVLS / LOUDNESS VISIBLE" },
             std::pair { lvlsHistoryVisibleParameterId, "LVLS / HISTORY VISIBLE" } })
        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID { id, 1 }, name, true,
            juce::AudioParameterBoolAttributes().withAutomatable(false).withMeta(true)));

    for (const auto& [id, name] : std::array {
             std::pair { lvlsHistoryMomentaryVisibleParameterId, "LVLS / HISTORY MOMENTARY" },
             std::pair { lvlsHistoryShortTermVisibleParameterId, "LVLS / HISTORY SHORT TERM" },
             std::pair { lvlsHistoryIntegratedVisibleParameterId, "LVLS / HISTORY INTEGRATED" },
             std::pair { lvlsHistoryZoomParameterId, "LVLS / HISTORY ZOOM" } })
        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID { id, 1 }, name, true,
            juce::AudioParameterBoolAttributes().withAutomatable(false).withMeta(true)));

    const juce::StringArray scopChannelModes { "LEFT", "RIGHT", "MID", "SIDE", "LR", "MS" };
    for (size_t bandIndex = 0; bandIndex < scopChannelModeParameterIds.size(); ++bandIndex)
    {
        layout.add(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID { scopChannelModeParameterIds[bandIndex], 1 },
            "SCOP / BAND " + juce::String(static_cast<int>(bandIndex + 1)) + " / CHANNEL",
            scopChannelModes,
            static_cast<int>(ana::scop::defaultChannelMode),
            juce::AudioParameterChoiceAttributes().withAutomatable(false).withMeta(true)));

        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { scopVerticalZoomParameterIds[bandIndex], 1 },
            "SCOP / BAND " + juce::String(static_cast<int>(bandIndex + 1)) + " / ZOOM",
            juce::NormalisableRange<float> { ana::scop::minimumVerticalZoomDecibels,
                                             ana::scop::maximumVerticalZoomDecibels,
                                             ana::scop::verticalZoomStepDecibels },
            ana::scop::defaultVerticalZoomDecibels,
            juce::AudioParameterFloatAttributes()
                .withAutomatable(false)
                .withMeta(true)
                .withStringFromValueFunction([] (const float value, int)
                {
                    const auto prefix = value > 0.05f ? "+" : "";
                    return prefix + juce::String(std::abs(value) < 0.05f ? 0.0f : value, 1);
                })));

        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID { scopNormalizeParameterIds[bandIndex], 1 },
            "SCOP / BAND " + juce::String(static_cast<int>(bandIndex + 1)) + " / NORMALIZE",
            ana::scop::defaultBandNormalized,
            juce::AudioParameterBoolAttributes().withAutomatable(false).withMeta(true)));
    }

    layout.add(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID { crossoverCountParameterId, 1 },
        "CROSSOVER / COUNT",
        0,
        static_cast<int>(ana::dsp::LinkwitzRileyCrossover::numCrossovers),
        static_cast<int>(ana::dsp::LinkwitzRileyCrossover::numCrossovers),
        juce::AudioParameterIntAttributes().withAutomatable(false).withMeta(true)));

    constexpr auto& defaults = ana::dsp::LinkwitzRileyCrossover::defaultFrequencies;

    for (size_t index = 0; index < defaults.size(); ++index)
    {
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { crossoverParameterIds[index], 1 },
            "CROSSOVER / " + juce::String(static_cast<int>(index + 1)),
            juce::NormalisableRange<float> { ana::analyzer_frequency::minimumHz,
                                             ana::analyzer_frequency::maximumHz, ana::analyzer_frequency::parameterStepHz },
            static_cast<float>(defaults[index]),
            juce::AudioParameterFloatAttributes()
                .withAutomatable(false)
                .withMeta(true)
                .withStringFromValueFunction([] (const float value, int)
                {
                    return formatFrequency(value);
                })));
    }

    return layout;
}
