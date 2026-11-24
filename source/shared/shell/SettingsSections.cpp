#include "SettingsSections.h"
#include "shared/shell/SubmoduleParameters.h"
#include "Processor.h"
#include "shared/shell/StereoFftStream.h"
#include "shared/spec/FrequencyScale.h"
#include "shared/scop/NoteLengths.h"
#include "shared/shell/GraphColours.h"
#include "shared/spec/Settings.h"
#include "shared/spec/MonitorMode.h"

#include <array>
#include <cmath>

namespace
{
juce::String formatIntegerValue(const double value)
{
    return juce::String(juce::roundToInt(value));
}

template <size_t ChoiceCount>
juce::String formatChoice(const double value,
                          const std::array<const char*, ChoiceCount>& choices)
{
    return choices[static_cast<size_t>(juce::jlimit(
        0, static_cast<int>(ChoiceCount) - 1, juce::roundToInt(value)))];
}

juce::String formatPercent(const double value)
{
    return juce::String(juce::roundToInt(value * 100.0));
}

juce::String formatFftSizeChoice(const double value)
{
    const auto& sizes = ana::fft::StereoFftStream::supportedFftSizes;
    return juce::String(sizes[static_cast<size_t>(juce::jlimit(
        0, static_cast<int>(sizes.size()) - 1, juce::roundToInt(value)))]);
}

juce::String formatAverageMaximumChoice(const double value)
{
    constexpr std::array<const char*, 2> choices { "AVG", "MAX" };
    return formatChoice(value, choices);
}

juce::String formatAverageMinimumChoice(const double value)
{
    constexpr std::array<const char*, 2> choices { "AVG", "MIN" };
    return formatChoice(value, choices);
}

juce::String formatFrequencyScaleChoice(const double value)
{
    constexpr std::array<const char*, 4> choices { "LIN", "EXT-LOG", "LOG", "MEL" };
    return formatChoice(value, choices);
}

juce::String formatGraphColourChoice(const double value)
{
    return ana::ui::graphColourOptions[static_cast<size_t>(
        ana::ui::clampGraphColourIndex(juce::roundToInt(value)))].name;
}

juce::String formatSignedValue(const double value)
{
    const auto displayValue = std::abs(value) < 0.05 ? 0.0 : value;
    return juce::String::formatted("%+.1f", displayValue);
}

juce::String formatSlopeValue(const double value)
{
    const auto displayValue = std::abs(value) < 0.005 ? 0.0 : value;
    return juce::String::formatted("%+.2f", displayValue);
}
}

ScopSettingsSection::ScopSettingsSection(PluginProcessor& processor)
    : overallMonitorControl(processor.getParameters(), PluginProcessor::scopOverallMonitorParameterId,
                            "OVERALL\nMONITOR", [] (const double value)
                            {
                                const auto index = static_cast<size_t>(juce::jlimit(0,
                                    static_cast<int>(ana::scopChannelModeCount) - 1,
                                    juce::roundToInt(value)));
                                constexpr std::array<const char*, ana::scopChannelModeCount> labels {
                                    "L", "R", "M", "S", "LR", "MS", "ST"
                                };
                                return juce::String(labels[index]);
                            }),
      crossoverCountControl(processor.getParameters(), PluginProcessor::crossoverCountParameterId,
                            "XOVERS", formatIntegerValue),
      styleControl(processor.getParameters(), PluginProcessor::scopStyleParameterId,
                   "STYLE", [] (const double value)
                   {
                       return value < 0.5 ? juce::String("FILLED") : juce::String("OUTLINE");
                   }),
      opacityControl(processor.getParameters(), PluginProcessor::scopOpacityParameterId,
                     "OPACITY", formatIntegerValue),
      slopeControl(processor.getParameters(), PluginProcessor::scopSlopeParameterId,
                   "SLOPE", formatSlopeValue),
      timeControl(processor.getParameters(), PluginProcessor::scopTimeParameterId,
                  "TIME", formatIntegerValue),
      timeNoteControl(processor.getParameters(), PluginProcessor::scopNoteLengthParameterId,
                      "TIME", [] (const double value)
                      {
                          return juce::String(ana::scop::noteLengthLabels[static_cast<size_t>(
                              ana::scop::clampNoteLengthIndex(juce::roundToInt(value)))]);
                      })
{
    slopeControl.setDragUnitStep(0.01);
}

SpecSettingsSection::SpecSettingsSection(PluginProcessor& processor, bool map)
    : isMap(map), monitorModeControl(processor.getParameters(),
                         ana::submodules::selectedId(PluginProcessor::specMonitorModeParameterId, map, 0),
                         map ? "MAP\nMONITOR" : "FREQ\nMONITOR", [] (const double value)
                         {
                             const auto index = static_cast<size_t>(juce::jlimit(0,
                                 static_cast<int>(ana::spec::monitorModeCount) - 1, juce::roundToInt(value)));
                             return juce::String(ana::spec::monitorModeLabels[index]);
                         }), fftSizeControl(processor.getParameters(), ana::submodules::selectedId(PluginProcessor::specFftSizeParameterId, map, 0),
                     "FFT-SIZE", formatFftSizeChoice),
      fftOverlapControl(processor.getParameters(), ana::submodules::selectedId(PluginProcessor::specFftOverlapParameterId, map, 0),
                     "FFT-OVERLAP", formatPercent),
      mapTimeOverlapControl(processor.getParameters(), ana::submodules::selectedId(PluginProcessor::specMapTimeOverlapParameterId, map, 0),
                        "TIME-OVERLAP", [] (const double value)
                        {
                            const auto factor = ana::spec::SpecProcessor::mapTimeOversamplingFactor(
                                juce::roundToInt(value));
                            return factor == 1 ? juce::String("NONE")
                                               : juce::String(factor);
                        }),
      mapTimeControl(processor.getParameters(), ana::submodules::selectedId(PluginProcessor::specMapTimeParameterId, map, 0),
                     "TIME", formatIntegerValue),
      mapTimeNoteControl(processor.getParameters(), ana::submodules::selectedId(PluginProcessor::specMapNoteLengthParameterId, map, 0),
                         "TIME", [] (const double value)
                         {
                             return juce::String(ana::scop::noteLengthLabels[static_cast<size_t>(
                                 ana::scop::clampNoteLengthIndex(juce::roundToInt(value)))]);
                         }),
      averageTimeControl(processor.getParameters(), ana::submodules::selectedId(PluginProcessor::specAverageTimeParameterId, map, 0),
                         "AVG-TIME", formatIntegerValue),
      smoothingControl(processor.getParameters(), ana::submodules::selectedId(PluginProcessor::specSmoothingParameterId, map, 0),
                       "SMOO\nTHING", formatIntegerValue),
      frequencyScaleControl(processor.getParameters(), ana::submodules::selectedId(PluginProcessor::specFrequencyScaleParameterId, map, 0),
                            "FREQ-SCALE", formatFrequencyScaleChoice),
      mapColourMapControl(processor.getParameters(), ana::submodules::selectedId(PluginProcessor::specMapColourMapParameterId, map, 0),
                          "COLOUR-MAP", [] (const double value)
                          {
                              const auto index = juce::jlimit(
                                  0, static_cast<int>(ana::spec::colourMapLabels.size()) - 1,
                                  juce::roundToInt(value));
                              return juce::String(ana::spec::colourMapLabels[static_cast<size_t>(index)]);
                          }),
      firstGraphTypeControl(processor.getParameters(), ana::submodules::selectedId(PluginProcessor::specFirstGraphTypeParameterId, map, 0),
                               "1ST-TYPE", formatAverageMaximumChoice),
      firstGraphColourControl(processor.getParameters(), ana::submodules::selectedId(PluginProcessor::specFirstGraphColourParameterId, map, 0),
                              "1ST-COLOUR", formatGraphColourChoice),
      secondGraphTypeControl(processor.getParameters(), ana::submodules::selectedId(PluginProcessor::specSecondGraphTypeParameterId, map, 0),
                                "2ND-TYPE", formatAverageMaximumChoice),
      secondGraphColourControl(processor.getParameters(), ana::submodules::selectedId(PluginProcessor::specSecondGraphColourParameterId, map, 0),
                               "2ND-COLOUR", formatGraphColourChoice),
      graphOpacityControl(processor.getParameters(), ana::submodules::selectedId(PluginProcessor::specGraphOpacityParameterId, map, 0),
                          "GRAPH-OPACITY", formatIntegerValue),
      slopeControl(processor.getParameters(), ana::submodules::selectedId(PluginProcessor::specSlopeParameterId, map, 0),
                   "SLOPE", formatSlopeValue),
      rangeLowControl(processor.getParameters(), ana::submodules::selectedId(PluginProcessor::specRangeLowParameterId, map, 0),
                      "AMP-LOW", [] (const double value) { return formatSignedValue(value); }),
      rangeHighControl(processor.getParameters(), ana::submodules::selectedId(PluginProcessor::specRangeHighParameterId, map, 0),
                       "AMP-HIGH", [] (const double value) { return formatSignedValue(value); })
{
    slopeControl.setDragUnitStep(0.01);
}

CorrSettingsSection::CorrSettingsSection(PluginProcessor& processor, int mode)
    : submoduleMode(mode), fftSizeControl(processor.getParameters(), ana::submodules::selectedId(PluginProcessor::corrFftSizeParameterId, false, mode),
                     "FFT-SIZE", formatFftSizeChoice),
      fftOverlapControl(processor.getParameters(), ana::submodules::selectedId(PluginProcessor::corrFftOverlapParameterId, false, mode),
                     "FFT-OVERLAP", formatPercent),
      averageTimeControl(processor.getParameters(), ana::submodules::selectedId(PluginProcessor::corrAverageTimeParameterId, false, mode),
                         "AVG-TIME", formatIntegerValue),
      smoothingControl(processor.getParameters(), ana::submodules::selectedId(PluginProcessor::corrSmoothingParameterId, false, mode),
                       "SMOO\nTHING", formatIntegerValue),
      frequencyScaleControl(processor.getParameters(), ana::submodules::selectedId(PluginProcessor::corrFrequencyScaleParameterId, false, mode),
                            "FREQ-SCALE", formatFrequencyScaleChoice),
      firstGraphTypeControl(processor.getParameters(), ana::submodules::selectedId(PluginProcessor::corrFirstGraphTypeParameterId, false, mode),
                            "1ST-TYPE", formatAverageMinimumChoice),
      firstGraphColourControl(processor.getParameters(), ana::submodules::selectedId(PluginProcessor::corrFirstGraphColourParameterId, false, mode),
                              "1ST-COLOUR", formatGraphColourChoice),
      secondGraphTypeControl(processor.getParameters(), ana::submodules::selectedId(PluginProcessor::corrSecondGraphTypeParameterId, false, mode),
                             "2ND-TYPE", formatAverageMinimumChoice),
      secondGraphColourControl(processor.getParameters(), ana::submodules::selectedId(PluginProcessor::corrSecondGraphColourParameterId, false, mode),
                               "2ND-COLOUR", formatGraphColourChoice),
      graphOpacityControl(processor.getParameters(), ana::submodules::selectedId(PluginProcessor::corrGraphOpacityParameterId, false, mode),
                          "GRAPH-OPACITY", formatIntegerValue)
{
}

LvlsSettingsSection::LvlsSettingsSection(PluginProcessor& processor)
    : peakRangeHighControl(processor.getParameters(), PluginProcessor::lvlsPeakRangeHighParameterId,
                           "PEAK/RMS-HIGH", formatSignedValue),
      peakRangeLowControl(processor.getParameters(), PluginProcessor::lvlsPeakRangeLowParameterId,
                          "PEAK/RMS-LOW", formatSignedValue),
      rmsWindowControl(processor.getParameters(), PluginProcessor::lvlsRmsWindowMsParameterId,
                       "RMS-WINDOW", formatIntegerValue),
      peakHoldControl(processor.getParameters(), PluginProcessor::lvlsPeakHoldMsParameterId,
                      "PEAK-HOLD", formatIntegerValue),
      readoutIntervalControl(processor.getParameters(), PluginProcessor::lvlsReadoutIntervalMsParameterId,
                             "READOUT-INTERVAL", formatIntegerValue),
      loudnessRangeHighControl(processor.getParameters(), PluginProcessor::lvlsLoudnessRangeHighParameterId,
                               "LUFS-HIGH", formatSignedValue),
      loudnessRangeLowControl(processor.getParameters(), PluginProcessor::lvlsLoudnessRangeLowParameterId,
                              "LUFS-LOW", formatSignedValue)
{
}
