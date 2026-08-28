#pragma once

#include <JuceHeader.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <utility>

namespace ana::fft
{
struct StereoFftFrame
{
    const float* left = nullptr;
    const float* right = nullptr;
    int size = 0;
};

class StereoFftStream
{
public:
    static constexpr std::array<int, 6> supportedBlockSizes { 512, 1024, 2048, 4096, 8192, 16384 };
    static constexpr int maximumFftSize = supportedBlockSizes.back();

    StereoFftStream();

    void prepare(double newSampleRate) noexcept;
    void reset() noexcept;
    double getSampleRate() const noexcept { return sampleRate; }

    template <typename FrameHandler>
    void processBlock(const juce::AudioBuffer<float>& buffer, const int requestedBlockSize,
                      const float overlap, FrameHandler&& onFrame) noexcept
    {
        if (buffer.getNumSamples() <= 0 || buffer.getNumChannels() <= 0)
            return;

        const auto fftIndex = getFftIndex(requestedBlockSize);
        const auto fftSize = supportedBlockSizes[static_cast<size_t>(fftIndex)];
        const auto hopSize = std::max(1, juce::roundToInt(static_cast<float>(fftSize)
                                                          * (1.0f - juce::jlimit(0.0f, 0.95f, overlap))));

        if (activeFftIndex != fftIndex)
        {
            activeFftIndex = fftIndex;
            samplesSinceAnalysis = 0;
        }

        const auto* left = buffer.getReadPointer(0);
        const auto* right = buffer.getReadPointer(std::min(1, buffer.getNumChannels() - 1));
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            leftRing[static_cast<size_t>(ringWritePosition)] = left[sample];
            rightRing[static_cast<size_t>(ringWritePosition)] = right[sample];
            ringWritePosition = (ringWritePosition + 1) & (maximumFftSize - 1);

            if (++samplesSinceAnalysis >= hopSize)
            {
                samplesSinceAnalysis = 0;
                analyse(fftIndex, std::forward<FrameHandler>(onFrame));
            }
        }
    }

private:
    int getFftIndex(int blockSize) const noexcept;

    template <typename FrameHandler>
    void analyse(const int fftIndex, FrameHandler&& onFrame) noexcept
    {
        const auto fftSize = supportedBlockSizes[static_cast<size_t>(fftIndex)];
        const auto start = (ringWritePosition - fftSize) & (maximumFftSize - 1);
        std::fill(leftFftBuffer.begin(), leftFftBuffer.end(), 0.0f);
        std::fill(rightFftBuffer.begin(), rightFftBuffer.end(), 0.0f);

        for (int index = 0; index < fftSize; ++index)
        {
            const auto ringIndex = (start + index) & (maximumFftSize - 1);
            const auto window = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi
                                                        * static_cast<float>(index)
                                                        / static_cast<float>(fftSize - 1));
            leftFftBuffer[static_cast<size_t>(index)] = leftRing[static_cast<size_t>(ringIndex)] * window;
            rightFftBuffer[static_cast<size_t>(index)] = rightRing[static_cast<size_t>(ringIndex)] * window;
        }

        ffts[static_cast<size_t>(fftIndex)]->performRealOnlyForwardTransform(leftFftBuffer.data(), true);
        ffts[static_cast<size_t>(fftIndex)]->performRealOnlyForwardTransform(rightFftBuffer.data(), true);
        onFrame(StereoFftFrame { leftFftBuffer.data(), rightFftBuffer.data(), fftSize });
    }

    std::array<std::unique_ptr<juce::dsp::FFT>, supportedBlockSizes.size()> ffts;
    std::array<float, maximumFftSize> leftRing {};
    std::array<float, maximumFftSize> rightRing {};
    std::array<float, maximumFftSize * 2> leftFftBuffer {};
    std::array<float, maximumFftSize * 2> rightFftBuffer {};
    double sampleRate = 44100.0;
    int ringWritePosition = 0;
    int samplesSinceAnalysis = 0;
    int activeFftIndex = -1;
};
} // namespace ana::fft
