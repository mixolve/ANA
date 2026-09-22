#include "SettingsSections.h"
#include "Processor.h"
#include "shell/StereoFftStream.h"
#include "shared/analyzer/Frequency.h"
#include "shared/scop/TimeScale.h"
#include "shared/shell/GraphColours.h"
#include "shared/spec/Settings.h"

#include <array>
#include <cmath>

namespace
{
juce::String formatFftSizeChoice(const double value)
{
    const auto& sizes = ana::fft::StereoFftStream::supportedFftSizes;
    return juce::String(sizes[static_cast<size_t>(juce::jlimit(
        0, static_cast<int>(sizes.size()) - 1, juce::roundToInt(value)))]);
}

juce::String formatAverageMaximumChoice(const double value)
{
    constexpr std::array<const char*, 2> choices { "AVG", "MAX" };
    return choices[static_cast<size_t>(juce::jlimit(
        0, static_cast<int>(choices.size()) - 1, juce::roundToInt(value)))];
}

juce::String formatAverageMinimumChoice(const double value)
{
    constexpr std::array<const char*, 2> choices { "AVG", "MIN" };
    return choices[static_cast<size_t>(juce::jlimit(
        0, static_cast<int>(choices.size()) - 1, juce::roundToInt(value)))];
}

juce::String formatFrequencyScaleChoice(const double value)
{
    constexpr std::array<const char*, 4> choices { "LIN", "EXT-LOG", "LOG", "MEL" };
    return choices[static_cast<size_t>(juce::jlimit(
        0, static_cast<int>(choices.size()) - 1, juce::roundToInt(value)))];
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
}

ScopSettingsSection::ScopSettingsSection(PluginProcessor& processor)
    : styleControl(processor.getParameters(), PluginProcessor::scopStyleParameterId,
                   "STYLE", [] (const double value)
                   {
                       return value < 0.5 ? juce::String("FILLED") : juce::String("OUTLINE");
                   }),
      opacityControl(processor.getParameters(), PluginProcessor::scopOpacityParameterId,
                     "OPACITY", [] (const double value)
                     {
                         return juce::String(juce::roundToInt(value));
                     }),
      timeControl(processor.getParameters(), PluginProcessor::scopTimeParameterId,
                  "TIME", [] (const double value)
                  {
                      return juce::String(juce::roundToInt(value));
                  }),
      timeNoteControl(processor.getParameters(), PluginProcessor::scopNoteLengthParameterId,
                      "TIME", [] (const double value)
                      {
                          return juce::String(ana::scop::noteLengthLabels[static_cast<size_t>(
                              ana::scop::clampNoteLengthIndex(juce::roundToInt(value)))]);
                      }),
      timeBaseControl(processor.getParameters(), PluginProcessor::scopTimeBaseParameterId,
                      "TIME-BASE", [] (const double value)
                      {
                          return value < 0.5 ? juce::String("MS") : juce::String("NOTE");
                      })
{
}

SpecSettingsSection::SpecSettingsSection(PluginProcessor& processor)
    : fftSizeControl(processor.getParameters(), PluginProcessor::specFftSizeParameterId,
                     "FFT-SIZE", formatFftSizeChoice),
      fftOverlapControl(processor.getParameters(), PluginProcessor::specFftOverlapParameterId,
                     "FFT-OVERLAP", [] (const double value) { return juce::String(juce::roundToInt(value * 100.0)); }),
      mapTimeOverlapControl(processor.getParameters(), PluginProcessor::specMapTimeOverlapParameterId,
                        "TIME-OVERLAP", [] (const double value)
                        {
                            const auto factor = ana::spec::SpecProcessor::mapTimeOversamplingFactor(
                                juce::roundToInt(value));
                            return factor == 1 ? juce::String("NONE")
                                               : juce::String(factor) + "x";
                        }),
      mapTimeControl(processor.getParameters(), PluginProcessor::specMapTimeParameterId,
                     "TIME", [] (const double value)
                     {
                         return juce::String(juce::roundToInt(value));
                     }),
      mapTimeNoteControl(processor.getParameters(), PluginProcessor::specMapNoteLengthParameterId,
                         "TIME", [] (const double value)
                         {
                             return juce::String(ana::scop::noteLengthLabels[static_cast<size_t>(
                                 ana::scop::clampNoteLengthIndex(juce::roundToInt(value)))]);
                         }),
      mapTimeBaseControl(processor.getParameters(), PluginProcessor::specMapTimeBaseParameterId,
                         "TIME-BASE", [] (const double value)
                         {
                             return value < 0.5 ? juce::String("MS") : juce::String("NOTE");
                         }),
      averageTimeControl(processor.getParameters(), PluginProcessor::specAverageTimeParameterId,
                         "AVG-TIME", [] (const double value) { return juce::String(juce::roundToInt(value)); }),
      smoothingControl(processor.getParameters(), PluginProcessor::specSmoothingParameterId,
                       "SMOOTHING", [] (const double value) { return juce::String(juce::roundToInt(value)); }),
      frequencyScaleControl(processor.getParameters(), PluginProcessor::specFrequencyScaleParameterId,
                            "FREQ-SCALE", formatFrequencyScaleChoice),
      mapColourMapControl(processor.getParameters(), PluginProcessor::specMapColourMapParameterId,
                          "COLOUR-MAP", [] (const double value)
                          {
                              const auto index = juce::jlimit(
                                  0, static_cast<int>(ana::spec::colourMapLabels.size()) - 1,
                                  juce::roundToInt(value));
                              return juce::String(ana::spec::colourMapLabels[static_cast<size_t>(index)]);
                          }),
      firstGraphTypeControl(processor.getParameters(), PluginProcessor::specFirstGraphTypeParameterId,
                               "1ST-TYPE", formatAverageMaximumChoice),
      firstGraphColourControl(processor.getParameters(), PluginProcessor::specFirstGraphColourParameterId,
                              "1ST-COLOUR", formatGraphColourChoice),
      secondGraphTypeControl(processor.getParameters(), PluginProcessor::specSecondGraphTypeParameterId,
                                "2ND-TYPE", formatAverageMaximumChoice),
      secondGraphColourControl(processor.getParameters(), PluginProcessor::specSecondGraphColourParameterId,
                               "2ND-COLOUR", formatGraphColourChoice),
      graphOpacityControl(processor.getParameters(), PluginProcessor::specGraphOpacityParameterId,
                          "GRAPH-OPACITY", [] (const double value)
                          {
                              return juce::String(juce::roundToInt(value)) + "%";
                          }),
      slopeControl(processor.getParameters(), PluginProcessor::specSlopeParameterId,
                   "SLOPE", formatSignedValue),
      rangeLowControl(processor.getParameters(), PluginProcessor::specRangeLowParameterId,
                      "AMP-LOW", [] (const double value) { return formatSignedValue(value) + " dB"; }),
      rangeHighControl(processor.getParameters(), PluginProcessor::specRangeHighParameterId,
                       "AMP-HIGH", [] (const double value) { return formatSignedValue(value) + " dB"; })
{
}

CorrSettingsSection::CorrSettingsSection(PluginProcessor& processor)
    : fftSizeControl(processor.getParameters(), PluginProcessor::corrFftSizeParameterId,
                     "FFT-SIZE", formatFftSizeChoice),
      fftOverlapControl(processor.getParameters(), PluginProcessor::corrFftOverlapParameterId,
                     "FFT-OVERLAP", [] (const double value) { return juce::String(juce::roundToInt(value * 100.0)); }),
      averageTimeControl(processor.getParameters(), PluginProcessor::corrAverageTimeParameterId,
                         "AVG-TIME", [] (const double value) { return juce::String(juce::roundToInt(value)); }),
      smoothingControl(processor.getParameters(), PluginProcessor::corrSmoothingParameterId,
                       "SMOOTHING", [] (const double value) { return juce::String(juce::roundToInt(value)); }),
      frequencyScaleControl(processor.getParameters(), PluginProcessor::corrFrequencyScaleParameterId,
                            "FREQ-SCALE", formatFrequencyScaleChoice),
      firstGraphTypeControl(processor.getParameters(), PluginProcessor::corrFirstGraphTypeParameterId,
                            "1ST-TYPE", formatAverageMinimumChoice),
      firstGraphColourControl(processor.getParameters(), PluginProcessor::corrFirstGraphColourParameterId,
                              "1ST-COLOUR", formatGraphColourChoice),
      secondGraphTypeControl(processor.getParameters(), PluginProcessor::corrSecondGraphTypeParameterId,
                             "2ND-TYPE", formatAverageMinimumChoice),
      secondGraphColourControl(processor.getParameters(), PluginProcessor::corrSecondGraphColourParameterId,
                               "2ND-COLOUR", formatGraphColourChoice),
      graphOpacityControl(processor.getParameters(), PluginProcessor::corrGraphOpacityParameterId,
                          "GRAPH-OPACITY", [] (const double value)
                          {
                              return juce::String(juce::roundToInt(value)) + "%";
                          })
{
}

LvlsSettingsSection::LvlsSettingsSection(PluginProcessor& processor)
    : widthControl(processor.getParameters(), PluginProcessor::lvlsWidthParameterId,
                   "LVLS-WIDTH", [] (const double value) { return juce::String(juce::roundToInt(value)); }),
      peakRangeHighControl(processor.getParameters(), PluginProcessor::lvlsPeakRangeHighParameterId,
                           "PEAK/RMS-HIGH", formatSignedValue),
      peakRangeLowControl(processor.getParameters(), PluginProcessor::lvlsPeakRangeLowParameterId,
                          "PEAK/RMS-LOW", formatSignedValue),
      rmsWindowControl(processor.getParameters(), PluginProcessor::lvlsRmsWindowMsParameterId,
                       "RMS-WINDOW", [] (const double value) { return juce::String(juce::roundToInt(value)); }),
      peakHoldControl(processor.getParameters(), PluginProcessor::lvlsPeakHoldMsParameterId,
                      "PEAK-HOLD", [] (const double value) { return juce::String(juce::roundToInt(value)); }),
      loudnessRangeHighControl(processor.getParameters(), PluginProcessor::lvlsLoudnessRangeHighParameterId,
                               "LUFS-HIGH", formatSignedValue),
      loudnessRangeLowControl(processor.getParameters(), PluginProcessor::lvlsLoudnessRangeLowParameterId,
                              "LUFS-LOW", formatSignedValue)
{
}
