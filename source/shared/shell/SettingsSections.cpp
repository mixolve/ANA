#include "SettingsSections.h"
#include "Processor.h"
#include "shell/StereoFftStream.h"
#include "shared/analyzer/Frequency.h"
#include "shared/scop/TimeScale.h"

#include <array>

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
                      const auto seconds = value / 1000.0;
                      return juce::String(seconds, seconds < 10.0 ? 1 : 0);
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
      averageTimeControl(processor.getParameters(), PluginProcessor::specAverageTimeParameterId,
                         "AVG-TIME", [] (const double value) { return juce::String(juce::roundToInt(value)); }),
      smoothingControl(processor.getParameters(), PluginProcessor::specSmoothingParameterId,
                       "SMOOTHING", [] (const double value) { return juce::String(juce::roundToInt(value)); }),
      frequencyScaleControl(processor.getParameters(), PluginProcessor::specFrequencyScaleParameterId,
                            "FREQ-SCALE", formatFrequencyScaleChoice),
      firstGraphTypeControl(processor.getParameters(), PluginProcessor::specFirstGraphTypeParameterId,
                               "1ST-TYPE", formatAverageMaximumChoice),
      secondGraphTypeControl(processor.getParameters(), PluginProcessor::specSecondGraphTypeParameterId,
                                "2ND-TYPE", formatAverageMaximumChoice),
      slopeControl(processor.getParameters(), PluginProcessor::specSlopeParameterId,
                   "SLOPE", [] (const double value) { return juce::String(value, 1); }),
      rangeLowControl(processor.getParameters(), PluginProcessor::specRangeLowParameterId,
                      "AMP-LOW", [] (const double value) { return juce::String(value, 1) + " dB"; }),
      rangeHighControl(processor.getParameters(), PluginProcessor::specRangeHighParameterId,
                       "AMP-HIGH", [] (const double value) { return juce::String(value, 1) + " dB"; })
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
      secondGraphTypeControl(processor.getParameters(), PluginProcessor::corrSecondGraphTypeParameterId,
                             "2ND-TYPE", formatAverageMinimumChoice)
{
}

LvlsSettingsSection::LvlsSettingsSection(PluginProcessor& processor)
    : widthControl(processor.getParameters(), PluginProcessor::lvlsWidthParameterId,
                   "LVLS-WIDTH", [] (const double value) { return juce::String(juce::roundToInt(value)); }),
      peakRangeHighControl(processor.getParameters(), PluginProcessor::lvlsPeakRangeHighParameterId,
                           "PEAK/RMS-HIGH", [] (const double value) { return juce::String(value, 1); }),
      peakRangeLowControl(processor.getParameters(), PluginProcessor::lvlsPeakRangeLowParameterId,
                          "PEAK/RMS-LOW", [] (const double value) { return juce::String(value, 1); }),
      rmsWindowControl(processor.getParameters(), PluginProcessor::lvlsRmsWindowMsParameterId,
                       "RMS-WINDOW", [] (const double value) { return juce::String(juce::roundToInt(value)); }),
      peakHoldControl(processor.getParameters(), PluginProcessor::lvlsPeakHoldMsParameterId,
                      "PEAK-HOLD", [] (const double value) { return juce::String(juce::roundToInt(value)); }),
      loudnessRangeHighControl(processor.getParameters(), PluginProcessor::lvlsLoudnessRangeHighParameterId,
                               "LUFS-HIGH", [] (const double value) { return juce::String(value, 1); }),
      loudnessRangeLowControl(processor.getParameters(), PluginProcessor::lvlsLoudnessRangeLowParameterId,
                              "LUFS-LOW", [] (const double value) { return juce::String(value, 1); })
{
}
