#include "StereoFftStream.h"

#include <cmath>

namespace ana::fft
{
StereoFftStream::StereoFftStream()
{
    // Cache every supported window to avoid rt allocation.
    // Use the fixed Gauss 200 dB sigma and scale the window to unit coherent gain.
    constexpr auto gaussian200Sigma = 0.147359f;

    for (size_t index = 0; index < ffts.size(); ++index)
    {
        ffts[index] = std::make_unique<juce::dsp::FFT>(
            juce::roundToInt(std::log2(supportedFftSizes[index])));
        const auto fftSize = supportedFftSizes[index];
        auto& hann = hannWindows[index];
        auto& gaussian200 = gaussian200Windows[index];
        hann.resize(static_cast<size_t>(fftSize));
        gaussian200.resize(static_cast<size_t>(fftSize));

        double hannSum = 0.0;
        double gaussian200Sum = 0.0;
        for (int sample = 0; sample < fftSize; ++sample)
        {
            const auto phase = static_cast<double>(sample)
                / static_cast<double>(std::max(1, fftSize - 1));
            const auto angle = juce::MathConstants<double>::twoPi * phase;
            hann[static_cast<size_t>(sample)] = static_cast<float>(
                0.5 - 0.5 * std::cos(angle));

            const auto gaussian200Normalised = 2.0f * static_cast<float>(sample)
                / static_cast<float>(fftSize - 1) - 1.0f;
            const auto gaussian200Value = std::exp(
                -0.5f * gaussian200Normalised * gaussian200Normalised
                    / (gaussian200Sigma * gaussian200Sigma));
            gaussian200[static_cast<size_t>(sample)] = gaussian200Value;
            hannSum += hann[static_cast<size_t>(sample)];
            gaussian200Sum += static_cast<double>(gaussian200Value);
        }

        hannCoherentGains[index] = static_cast<float>(hannSum / fftSize);
        const auto gaussian200CoherentGainCompensation = static_cast<float>(
            static_cast<double>(fftSize) / std::max(1.0, gaussian200Sum));
        juce::FloatVectorOperations::multiply(gaussian200.data(),
                                              gaussian200CoherentGainCompensation, fftSize);
        gaussian200CoherentGains[index] = 1.0f;
    }

    reset();
}

void StereoFftStream::prepare(const double newSampleRate) noexcept
{
    sampleRate = std::max(1.0, newSampleRate);
    reset();
}

void StereoFftStream::reset() noexcept
{
    leftRing.fill(0.0f);
    rightRing.fill(0.0f);
    leftFftBuffer.fill(0.0f);
    rightFftBuffer.fill(0.0f);
    ringWritePosition = 0;
    samplesSinceAnalysis = 0;
    validSampleCount = 0;
    activeFftIndex = -1;
    activeHopSize = -1;
    activeWindowType = WindowType::hann;
}

int StereoFftStream::getFftIndex(const int requestedFftSize) const noexcept
{
    for (int index = 0; index < static_cast<int>(supportedFftSizes.size()); ++index)
        if (requestedFftSize <= supportedFftSizes[static_cast<size_t>(index)])
            return index;

    return static_cast<int>(supportedFftSizes.size()) - 1;
}
}
