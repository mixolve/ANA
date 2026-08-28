#include "StereoCorrelationProcessor.h"

#include <algorithm>
#include <cmath>

namespace ana::corr
{
StereoCorrelationProcessor::StereoCorrelationProcessor()
{
    reset();
}

void StereoCorrelationProcessor::prepare(const double newSampleRate) noexcept
{
    fftStream.prepare(newSampleRate);
    reset();
}

void StereoCorrelationProcessor::reset() noexcept
{
    fftStream.reset();
    phaseAverage.fill(1.0f);
    amplitudeAverage.fill(1.0f);
    phaseMinimum.fill(1.0f);
    amplitudeMinimum.fill(1.0f);
    for (auto* values : { &publishedPhase, &publishedAmplitude,
                          &publishedPhaseMinimum, &publishedAmplitudeMinimum })
        for (auto& value : *values)
            value.store(1.0f, std::memory_order_relaxed);
    publishedFftSize.store(0, std::memory_order_release);
    lastFrameSize = 0;
    revision.fetch_add(1, std::memory_order_release);
}

void StereoCorrelationProcessor::requestClear() noexcept
{
    clearRequested.store(true, std::memory_order_release);
}

void StereoCorrelationProcessor::setFrozen(const bool shouldFreeze) noexcept
{
    frozen.store(shouldFreeze, std::memory_order_release);
}

bool StereoCorrelationProcessor::isFrozen() const noexcept
{
    return frozen.load(std::memory_order_acquire);
}

void StereoCorrelationProcessor::processBlock(const juce::AudioBuffer<float>& buffer, const int blockSize,
                                               const float overlap, const float averagingTimeMilliseconds) noexcept
{
    if (clearRequested.exchange(false, std::memory_order_acq_rel))
        reset();

    if (frozen.load(std::memory_order_acquire))
        return;

    fftStream.processBlock(buffer, blockSize, overlap,
                           [this, averagingTimeMilliseconds] (const fft::StereoFftFrame& frame)
                           {
                               publish(frame, averagingTimeMilliseconds);
                           });
}

void StereoCorrelationProcessor::copyCorrelation(const Mode mode, const DisplayType type,
                                                  std::vector<float>& destination, int& fftSize) const
{
    fftSize = publishedFftSize.load(std::memory_order_acquire);
    const auto binCount = fftSize > 0 ? fftSize / 2 + 1 : 0;
    destination.resize(static_cast<size_t>(binCount));
    const auto& source = mode == Mode::phase
        ? (type == DisplayType::realtimeAverage ? publishedPhase : publishedPhaseMinimum)
        : (type == DisplayType::realtimeAverage ? publishedAmplitude : publishedAmplitudeMinimum);
    for (int index = 0; index < binCount; ++index)
        destination[static_cast<size_t>(index)] = source[static_cast<size_t>(index)].load(std::memory_order_acquire);
}

double StereoCorrelationProcessor::getSampleRate() const noexcept
{
    return fftStream.getSampleRate();
}

uint64_t StereoCorrelationProcessor::getRevision() const noexcept
{
    return revision.load(std::memory_order_acquire);
}

void StereoCorrelationProcessor::publish(const fft::StereoFftFrame& frame,
                                         const float averagingTimeMilliseconds) noexcept
{
    const auto fftSize = frame.size;
    if (lastFrameSize != fftSize)
    {
        phaseAverage.fill(1.0f);
        amplitudeAverage.fill(1.0f);
        phaseMinimum.fill(1.0f);
        amplitudeMinimum.fill(1.0f);
        lastFrameSize = fftSize;
    }
    const auto binCount = fftSize / 2 + 1;
    const auto durationSeconds = static_cast<float>(fftSize) / static_cast<float>(fftStream.getSampleRate());
    const auto averagingSeconds = std::max(0.001f, averagingTimeMilliseconds * 0.001f);
    const auto alpha = std::exp(-durationSeconds / averagingSeconds);
    const auto update = 1.0f - alpha;
    const auto complexValue = [] (const float* data, const int bin)
    {
        return juce::Point<float>(data[2 * bin], data[2 * bin + 1]);
    };

    for (int index = 0; index < binCount; ++index)
    {
        const auto left = complexValue(frame.left, index);
        const auto right = complexValue(frame.right, index);
        const auto leftMagnitude = std::hypot(left.x, left.y);
        const auto rightMagnitude = std::hypot(right.x, right.y);
        const auto denominator = leftMagnitude * rightMagnitude;
        const auto phase = denominator > 1.0e-12f
            ? juce::jlimit(-1.0f, 1.0f, (left.x * right.x + left.y * right.y) / denominator)
            : 1.0f;

        phaseAverage[static_cast<size_t>(index)] = alpha * phaseAverage[static_cast<size_t>(index)]
            + update * phase;
        phaseMinimum[static_cast<size_t>(index)] = std::min(phaseMinimum[static_cast<size_t>(index)],
                                                             phaseAverage[static_cast<size_t>(index)]);
        const auto amplitudeDenominator = leftMagnitude * leftMagnitude + rightMagnitude * rightMagnitude;
        const auto amplitudeSimilarity = amplitudeDenominator > 1.0e-12f
            ? juce::jlimit(0.0f, 1.0f, 2.0f * leftMagnitude * rightMagnitude / amplitudeDenominator)
            : 1.0f;
        amplitudeAverage[static_cast<size_t>(index)] = alpha * amplitudeAverage[static_cast<size_t>(index)]
            + update * amplitudeSimilarity;
        amplitudeMinimum[static_cast<size_t>(index)] = std::min(amplitudeMinimum[static_cast<size_t>(index)],
                                                                 amplitudeAverage[static_cast<size_t>(index)]);

        publishedPhase[static_cast<size_t>(index)].store(phaseAverage[static_cast<size_t>(index)],
                                                           std::memory_order_relaxed);
        publishedAmplitude[static_cast<size_t>(index)].store(amplitudeAverage[static_cast<size_t>(index)],
                                                               std::memory_order_relaxed);
        publishedPhaseMinimum[static_cast<size_t>(index)].store(phaseMinimum[static_cast<size_t>(index)],
                                                                  std::memory_order_relaxed);
        publishedAmplitudeMinimum[static_cast<size_t>(index)].store(amplitudeMinimum[static_cast<size_t>(index)],
                                                                      std::memory_order_relaxed);
    }

    publishedFftSize.store(fftSize, std::memory_order_release);
    revision.fetch_add(1, std::memory_order_release);
}
} // namespace ana::corr
