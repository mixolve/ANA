#pragma once

#include "shared/shell/StereoFftStream.h"

namespace ana::offline
{
// Each caller starts a fresh stream for an item. Zero-pad its tail to measure
// centred windows through its end, including items shorter than an FFT window.
template <typename ProcessBlock, typename FrameHandler, typename Cancel, typename Progress>
bool readSpectrogramFrames(juce::AudioFormatReader& reader,
                           const juce::int64 start, const juce::int64 end,
                           const int fftSize, const int requestedHop,
                           const float gain, ProcessBlock&& processBlock,
                           FrameHandler&& onFrame, Cancel&& shouldCancel,
                           Progress&& onProgress)
{
    if (end <= start || fftSize <= 0)
        return true;
    constexpr int blockSize = 4096;
    const auto hop = static_cast<int>(std::min<juce::int64>(
        std::max(1, requestedHop), end - start));
    const auto paddedEnd = end + fftSize / 2;
    juce::AudioBuffer<float> buffer(2, blockSize);
    for (auto position = start; position < paddedEnd; position += blockSize)
    {
        if (shouldCancel())
            return false;
        const auto blockSamples = static_cast<int>(std::min<juce::int64>(
            blockSize, paddedEnd - position));
        const auto readSamples = static_cast<int>(std::clamp<juce::int64>(
            end - position, 0, blockSamples));
        buffer.clear();
        if (readSamples > 0)
        {
            if (! reader.read(&buffer, 0, readSamples, position, true, true))
                return false;
            buffer.applyGain(0, readSamples, gain);
        }
        juce::AudioBuffer<float> block(
            buffer.getArrayOfWritePointers(), buffer.getNumChannels(), blockSamples);
        processBlock(block, fftSize, hop, [&] (const fft::StereoFftFrame& frame)
        {
            const auto centre = static_cast<double>(position + frame.sampleOffset)
                - 0.5 * static_cast<double>(frame.size);
            if (centre >= static_cast<double>(start) && centre < static_cast<double>(end))
                onFrame(frame, centre);
        });
        onProgress(static_cast<float>(std::min(end, position + blockSamples) - start)
                   / static_cast<float>(end - start));
    }
    return true;
}
}
