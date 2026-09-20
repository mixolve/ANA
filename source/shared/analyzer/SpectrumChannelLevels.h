#pragma once

#include "StereoFftStream.h"

#include <algorithm>
#include <cmath>

namespace ana::fft
{
struct StereoSpectrumLevels
{
    float leftGain = 0.0f;
    float rightGain = 0.0f;
    float stereoGain = 0.0f;
    float midGain = 0.0f;
    float sideGain = 0.0f;
    float deltaGain = 0.0f;
    float leftDecibels = 0.0f;
    float rightDecibels = 0.0f;
    float stereoDecibels = 0.0f;
    float midDecibels = 0.0f;
    float sideDecibels = 0.0f;
    float deltaDecibels = 0.0f;
};

inline float spectrumGainToDecibels(const float gain, const float minimumDecibels) noexcept
{
    return juce::jmax(minimumDecibels,
                      juce::Decibels::gainToDecibels(gain, minimumDecibels));
}

inline StereoSpectrumLevels calculateStereoSpectrumLevels(
    const StereoFftFrame& frame,
    const int bin,
    const float magnitudeScale,
    const float minimumDecibels) noexcept
{
    const auto complexBin = [bin] (const float* data)
    {
        return juce::Point<float> { data[2 * bin], data[2 * bin + 1] };
    };
    const auto magnitude = [] (const juce::Point<float> value)
    {
        return std::hypot(value.x, value.y);
    };
    const auto denominator = static_cast<float>(std::max(1, frame.size))
        * std::max(1.0e-12f, frame.windowCoherentGain);
    const auto calibratedGain = [denominator, magnitudeScale] (const float value)
    {
        return std::max(0.0f, value * magnitudeScale / denominator);
    };

    const auto leftComplex = complexBin(frame.left);
    const auto rightComplex = complexBin(frame.right);
    const auto midComplex = (leftComplex + rightComplex) * 0.5f;
    const auto sideComplex = (leftComplex - rightComplex) * 0.5f;

    StereoSpectrumLevels levels;
    levels.leftGain = calibratedGain(magnitude(leftComplex));
    levels.rightGain = calibratedGain(magnitude(rightComplex));
    // STEREO averages magnitudes; MID/SIDE remain phase-aware transforms.
    levels.stereoGain = calibratedGain((magnitude(leftComplex) + magnitude(rightComplex)) * 0.5f);
    levels.midGain = calibratedGain(magnitude(midComplex));
    levels.sideGain = calibratedGain(magnitude(sideComplex));
    // DELTA compares channel magnitudes, unlike phase-aware SIDE.
    levels.deltaGain = std::abs(levels.leftGain - levels.rightGain);

    levels.leftDecibels = spectrumGainToDecibels(levels.leftGain, minimumDecibels);
    levels.rightDecibels = spectrumGainToDecibels(levels.rightGain, minimumDecibels);
    levels.stereoDecibels = spectrumGainToDecibels(levels.stereoGain, minimumDecibels);
    levels.midDecibels = spectrumGainToDecibels(levels.midGain, minimumDecibels);
    levels.sideDecibels = spectrumGainToDecibels(levels.sideGain, minimumDecibels);
    levels.deltaDecibels = spectrumGainToDecibels(levels.deltaGain, minimumDecibels);
    return levels;
}
}
