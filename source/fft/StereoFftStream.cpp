#include "StereoFftStream.h"

namespace ana::fft
{
StereoFftStream::StereoFftStream()
{
    for (size_t index = 0; index < ffts.size(); ++index)
        ffts[index] = std::make_unique<juce::dsp::FFT>(
            juce::roundToInt(std::log2(supportedBlockSizes[index])));

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
    activeFftIndex = -1;
}

int StereoFftStream::getFftIndex(const int blockSize) const noexcept
{
    for (int index = 0; index < static_cast<int>(supportedBlockSizes.size()); ++index)
        if (blockSize <= supportedBlockSizes[static_cast<size_t>(index)])
            return index;

    return static_cast<int>(supportedBlockSizes.size()) - 1;
}
} // namespace ana::fft
