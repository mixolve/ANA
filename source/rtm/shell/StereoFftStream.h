#pragma once

#include <cstddef>
#include <JuceHeader.h>

#include <algorithm>
#include <array>
#include <memory>
#include <utility>
#include <vector>

namespace ana::fft
{
enum class WindowType
{
    hann,
    gaussian200Db
};

struct StereoFftFrame
{
    const float* left = nullptr;
    const float* right = nullptr;
    int size = 0;
    int hopSize = 0;
    float windowCoherentGain = 0.5f;
    int sampleOffset = 0;
};

class StereoFftStream
{
public:
    static constexpr std::array<int, 6> supportedFftSizes { 512, 1024, 2048, 4096, 8192, 16384 };
    static constexpr size_t defaultFftSizeIndex = 2;
    static constexpr int defaultFftSize = supportedFftSizes[defaultFftSizeIndex];
    static constexpr int maximumFftSize = supportedFftSizes.back();
    static_assert((maximumFftSize & (maximumFftSize - 1)) == 0,
                  "FFT ring buffer size must be a power of two");
    static constexpr float minimumOverlap = 0.0f;
    static constexpr float maximumOverlap = 0.95f;
    static constexpr float overlapParameterStep = 0.01f;
    static constexpr float defaultOverlap = 0.75f;

    static constexpr bool isSupportedFftSize(const int fftSize) noexcept
    {
        for (const auto supportedSize : supportedFftSizes)
            if (fftSize == supportedSize)
                return true;
        return false;
    }

    StereoFftStream();

    void prepare(double newSampleRate) noexcept;
    void reset() noexcept;
    double getSampleRate() const noexcept { return sampleRate; }

    template <typename FrameHandler>
    void processBlock(const juce::AudioBuffer<float>& buffer, const int requestedFftSize,
                      const float fftOverlap, FrameHandler&& onFrame) noexcept
    {
        processBlock(buffer, requestedFftSize, fftOverlap, WindowType::hann,
                     std::forward<FrameHandler>(onFrame));
    }

    template <typename FrameHandler>
    void processBlock(const juce::AudioBuffer<float>& buffer, const int requestedFftSize,
                      const float fftOverlap, const WindowType windowType,
                      FrameHandler&& onFrame) noexcept
    {
        const auto fftIndex = getFftIndex(requestedFftSize);
        const auto fftSize = supportedFftSizes[static_cast<size_t>(fftIndex)];
        const auto hopSize = std::max(1, juce::roundToInt(static_cast<float>(fftSize)
                                                          * (1.0f - juce::jlimit(minimumOverlap, maximumOverlap, fftOverlap))));
        processBlockWithHop(buffer, requestedFftSize, hopSize, windowType,
                            std::forward<FrameHandler>(onFrame));
    }


    template <typename FrameHandler>
    void processBlockWithHop(const juce::AudioBuffer<float>& buffer,
                             const int requestedFftSize,
                             const int requestedHopSize,
                             const WindowType windowType,
                             FrameHandler&& onFrame) noexcept
    {
        if (buffer.getNumSamples() <= 0 || buffer.getNumChannels() <= 0)
            return;

        const auto fftIndex = getFftIndex(requestedFftSize);
        const auto fftSize = supportedFftSizes[static_cast<size_t>(fftIndex)];
        const auto hopSize = std::max(1, requestedHopSize);

        // FFT size, hop and window changes invalidate partial frame progress.
        if (activeFftIndex != fftIndex || activeWindowType != windowType)
        {
            activeFftIndex = fftIndex;
            activeHopSize = hopSize;
            activeWindowType = windowType;
            samplesSinceAnalysis = 0;
            validSampleCount = 0;
        }
        else if (activeHopSize != hopSize)
        {
            activeHopSize = hopSize;
            samplesSinceAnalysis = 0;
        }

        const auto* left = buffer.getReadPointer(0);
        const auto* right = buffer.getReadPointer(std::min(1, buffer.getNumChannels() - 1));
        auto inputPosition = 0;
        auto samplesRemaining = buffer.getNumSamples();
        while (samplesRemaining > 0)
        {
            const auto samplesUntilAnalysis = std::max(1, hopSize - samplesSinceAnalysis);
            // Explicit hops may exceed the ring; chunk writes to at most one ring length.
            const auto samplesToCopy = std::min(
                std::min(samplesRemaining, samplesUntilAnalysis), maximumFftSize);
            const auto firstPart = std::min(samplesToCopy, maximumFftSize - ringWritePosition);
            const auto secondPart = samplesToCopy - firstPart;

            juce::FloatVectorOperations::copy(leftRing.data() + ringWritePosition,
                                              left + inputPosition, firstPart);
            juce::FloatVectorOperations::copy(rightRing.data() + ringWritePosition,
                                              right + inputPosition, firstPart);
            if (secondPart > 0)
            {
                juce::FloatVectorOperations::copy(leftRing.data(),
                                                  left + inputPosition + firstPart, secondPart);
                juce::FloatVectorOperations::copy(rightRing.data(),
                                                  right + inputPosition + firstPart, secondPart);
            }

            ringWritePosition = (ringWritePosition + samplesToCopy) & (maximumFftSize - 1);
            samplesSinceAnalysis += samplesToCopy;
            validSampleCount = std::min(fftSize, validSampleCount + samplesToCopy);
            inputPosition += samplesToCopy;
            samplesRemaining -= samplesToCopy;

            if (samplesSinceAnalysis >= hopSize)
            {
                samplesSinceAnalysis = 0;
                if (validSampleCount >= fftSize)
                    analyse(fftIndex, hopSize, windowType, 0, std::forward<FrameHandler>(onFrame));
            }
        }
    }

private:
    int getFftIndex(int requestedFftSize) const noexcept;

    template <typename FrameHandler>
    void analyse(const int fftIndex, const int hopSize, const WindowType windowType,
                 const int sampleOffset, FrameHandler&& onFrame) noexcept
    {
        const auto fftSize = supportedFftSizes[static_cast<size_t>(fftIndex)];
        const auto start = (ringWritePosition - fftSize) & (maximumFftSize - 1);
        const auto firstPart = std::min(fftSize, maximumFftSize - start);
        const auto secondPart = fftSize - firstPart;
        juce::FloatVectorOperations::copy(leftFftBuffer.data(), leftRing.data() + start, firstPart);
        juce::FloatVectorOperations::copy(rightFftBuffer.data(), rightRing.data() + start, firstPart);
        if (secondPart > 0)
        {
            juce::FloatVectorOperations::copy(leftFftBuffer.data() + firstPart,
                                              leftRing.data(), secondPart);
            juce::FloatVectorOperations::copy(rightFftBuffer.data() + firstPart,
                                              rightRing.data(), secondPart);
        }
        const auto windowIndex = static_cast<size_t>(fftIndex);
        const auto& window = windowType == WindowType::gaussian200Db
            ? gaussian200Windows[windowIndex] : hannWindows[windowIndex];
        const auto coherentGain = windowType == WindowType::gaussian200Db
            ? gaussian200CoherentGains[windowIndex] : hannCoherentGains[windowIndex];
        juce::FloatVectorOperations::multiply(leftFftBuffer.data(), window.data(), fftSize);
        juce::FloatVectorOperations::multiply(rightFftBuffer.data(), window.data(), fftSize);
        std::fill_n(leftFftBuffer.begin() + fftSize, fftSize, 0.0f);
        std::fill_n(rightFftBuffer.begin() + fftSize, fftSize, 0.0f);

        ffts[static_cast<size_t>(fftIndex)]->performRealOnlyForwardTransform(leftFftBuffer.data(), true);
        ffts[static_cast<size_t>(fftIndex)]->performRealOnlyForwardTransform(rightFftBuffer.data(), true);
        onFrame(StereoFftFrame { leftFftBuffer.data(), rightFftBuffer.data(), fftSize, hopSize,
                                 coherentGain, sampleOffset });
    }

    std::array<std::unique_ptr<juce::dsp::FFT>, supportedFftSizes.size()> ffts;
    std::array<std::vector<float>, supportedFftSizes.size()> hannWindows;
    std::array<std::vector<float>, supportedFftSizes.size()> gaussian200Windows;
    std::array<float, supportedFftSizes.size()> hannCoherentGains {};
    std::array<float, supportedFftSizes.size()> gaussian200CoherentGains {};
    std::array<float, maximumFftSize> leftRing {};
    std::array<float, maximumFftSize> rightRing {};
    std::array<float, maximumFftSize * 2> leftFftBuffer {};
    std::array<float, maximumFftSize * 2> rightFftBuffer {};
    double sampleRate = 44100.0;
    int ringWritePosition = 0;
    int samplesSinceAnalysis = 0;
    int validSampleCount = 0;
    int activeFftIndex = -1;
    int activeHopSize = -1;
    WindowType activeWindowType = WindowType::hann;
};
}
