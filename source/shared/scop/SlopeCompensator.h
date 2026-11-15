#pragma once

#include "Settings.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <complex>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace ana::scop
{
// Minimum-phase, frequency-sampled FIR flat-tilt compensation for the SCOP
// display path. The filter precedes the band's crossover and is never applied
// to the host audio. A direct FIR head and FFT-partitioned tail keep the
// convolution causal with zero extra block latency and constant CPU cost.
class SlopeCompensator
{
public:
    struct StereoSample
    {
        double left = 0.0;
        double right = 0.0;
    };

    static constexpr size_t blockSize = 256;
    static constexpr size_t processingDelaySamples = 0;

    SlopeCompensator() = default;
    SlopeCompensator(const SlopeCompensator&) = delete;
    SlopeCompensator& operator=(const SlopeCompensator&) = delete;

    ~SlopeCompensator()
    {
        stopWorker();
        delete published.exchange(nullptr, std::memory_order_acq_rel);
        delete retired.exchange(nullptr, std::memory_order_acq_rel);
        delete activeKernel;
    }

    // Call outside the audio callback. Realtime instances use a worker to
    // rebuild FIR coefficients on automation changes; offline instances build
    // the requested coefficients synchronously before processing.
    void prepare(const double newSampleRate, const bool realtime = false)
    {
        stopWorker();
        delete published.exchange(nullptr, std::memory_order_acq_rel);
        delete retired.exchange(nullptr, std::memory_order_acq_rel);
        delete activeKernel;
        activeKernel = nullptr;

        sampleRate = std::clamp(newSampleRate, 1000.0, 384000.0);
        asynchronous = realtime;
        activeKernel = designKernel(sampleRate, defaultSlopeDecibelsPerOctave);
        requestedSlopeHundredth.store(static_cast<int>(std::lround(defaultSlopeDecibelsPerOctave * 100)),
                                  std::memory_order_relaxed);
        historyLength = recommendedTapCount(sampleRate) / blockSize;
        for (auto& channel : inputSpectra)
            channel.resize(historyLength);
        reset();
        if (asynchronous)
            worker = std::thread([this] { runWorker(); });
    }

    void reset() noexcept
    {
        for (auto& channel : inputBlock)
            channel.fill(0.0f);
        for (auto& channel : outputBlock)
            channel.fill(0.0f);
        for (auto& channel : overlap)
            channel.fill(0.0f);
        for (auto& channel : headHistory)
            channel.fill(0.0f);
        for (auto& channel : inputSpectra)
            for (auto& spectrum : channel)
                spectrum.fill({ 0.0f, 0.0f });
        blockCursor = 0;
        inputIndex = 0;
    }

    void setSlope(const float decibelsPerOctave)
    {
        const auto finiteSlope = std::isfinite(decibelsPerOctave) ? decibelsPerOctave : 0.0f;
        const auto requested = static_cast<int>(std::lround(std::clamp(
            finiteSlope, minimumSlopeDecibelsPerOctave, maximumSlopeDecibelsPerOctave) * 100.0f));
        if (asynchronous)
        {
            if (requestedSlopeHundredth.exchange(requested, std::memory_order_relaxed) != requested)
                wakeWorker.notify_one();
            return;
        }
        if (activeKernel != nullptr && activeKernel->slopeHundredth == requested)
            return;
        // Offline analysis uses a dedicated worker thread; no audio callback.
        delete activeKernel;
        activeKernel = designKernel(sampleRate, static_cast<double>(requested) / 100.0);
    }

    uint64_t getAppliedKernelRevision() const noexcept
    {
        return appliedKernelRevision.load(std::memory_order_acquire);
    }

    StereoSample processSample(const double left, const double right) noexcept
    {
        const auto inputs = std::array<float, 2> {
            static_cast<float>(std::isfinite(left) ? left : 0.0),
            static_cast<float>(std::isfinite(right) ? right : 0.0)
        };
        // At 0 dB/oct the kernel is exactly unity. Avoid 512 FIR-head
        // multiplies per stereo sample while maintaining the input history for
        // subsequent nonzero SLOPE automation.
        if (activeKernel != nullptr && activeKernel->slopeHundredth == 0)
        {
            for (size_t channel = 0; channel < 2; ++channel)
            {
                headHistory[channel][inputIndex] = inputs[channel];
                inputBlock[channel][inputIndex] = inputs[channel];
            }
            if (++inputIndex == blockSize)
            {
                inputIndex = 0;
                processCompletedBlock();
            }
            return { inputs[0], inputs[1] };
        }
        StereoSample output;
        for (size_t channel = 0; channel < 2; ++channel)
        {
            headHistory[channel][inputIndex] = inputs[channel];
            auto sum = outputBlock[channel][inputIndex];
            if (activeKernel != nullptr)
            {
                // The direct FIR head avoids block processing latency.
                size_t historyIndex = inputIndex;
                for (const auto tap : activeKernel->head)
                {
                    sum += tap * headHistory[channel][historyIndex];
                    historyIndex = (historyIndex + blockSize - 1) % blockSize;
                }
            }
            if (channel == 0) output.left = sum;
            else output.right = sum;
            inputBlock[channel][inputIndex] = inputs[channel];
        }
        if (++inputIndex == blockSize)
        {
            inputIndex = 0;
            processCompletedBlock();
        }
        return output;
    }

    static size_t recommendedTapCount(const double rate) noexcept
    {
        // Approximately 3 Hz of FIR frequency resolution at any host rate.
        size_t taps = 1024;
        while (static_cast<double>(taps) < std::min(rate / 3.0, 65536.0))
            taps *= 2;
        return taps;
    }

private:
    static constexpr size_t transformSize = blockSize * 2;
    static constexpr double pi = 3.1415926535897932384626433832795;
    static constexpr double pivotFrequency = 632.0;
    using Complex = std::complex<float>;
    using Spectrum = std::array<Complex, transformSize>;

    struct Kernel
    {
        int slopeHundredth = 0;
        size_t partitionCount = 0;
        std::array<float, blockSize> head {};
        std::vector<Spectrum> partitions;
    };

    template <typename T>
    static void fft(std::vector<std::complex<T>>& data, const bool inverse) noexcept
    {
        transform(data.data(), data.size(), inverse);
    }

    template <typename T>
    static void transform(std::complex<T>* data, const size_t length, const bool inverse) noexcept
    {
        for (size_t i = 1, j = 0; i < length; ++i)
        {
            auto bit = length >> 1;
            for (; j & bit; bit >>= 1)
                j ^= bit;
            j ^= bit;
            if (i < j)
                std::swap(data[i], data[j]);
        }
        for (size_t len = 2; len <= length; len <<= 1)
        {
            const auto angle = static_cast<T>((inverse ? 2.0 : -2.0) * pi / static_cast<double>(len));
            const std::complex<T> step { std::cos(angle), std::sin(angle) };
            for (size_t begin = 0; begin < length; begin += len)
            {
                std::complex<T> twiddle { 1, 0 };
                for (size_t index = 0; index < len / 2; ++index)
                {
                    const auto a = data[begin + index];
                    const auto b = data[begin + index + len / 2] * twiddle;
                    data[begin + index] = a + b;
                    data[begin + index + len / 2] = a - b;
                    twiddle *= step;
                }
            }
        }
        if (inverse)
            for (size_t i = 0; i < length; ++i)
                data[i] /= static_cast<T>(length);
    }

    static Kernel* designKernel(const double sampleRate, const double slope)
    {
        auto kernel = std::make_unique<Kernel>();
        kernel->slopeHundredth = static_cast<int>(std::lround(slope * 100.0));
        const auto tapCount = recommendedTapCount(sampleRate);
        kernel->partitionCount = tapCount / blockSize;
        kernel->partitions.resize(kernel->partitionCount);
        std::vector<double> impulse(tapCount, 0.0);
        if (kernel->slopeHundredth == 0)
            impulse[0] = 1.0;
        else
        {
            // Real cepstrum gives a causal minimum-phase spectral factor.
            // This avoids the long fixed group delay of a symmetric FIR.
            const size_t designSize = std::max<size_t>(32768, tapCount * 4);
            std::vector<std::complex<double>> cepstrum(designSize);
            const auto nyquist = sampleRate / 2.0;
            const auto effectiveFrequency = [nyquist](double frequency)
            {
                // Finite response at DC, smooth near the Nyquist edge. The
                // 20 Hz .. 90%-Nyquist region retains the flat-tilt target.
                frequency = std::sqrt(frequency * frequency + 3.0 * 3.0);
                const auto beginTaper = 0.9 * nyquist;
                if (frequency > beginTaper)
                {
                    const auto x = std::clamp((frequency - beginTaper)
                                              / (nyquist - beginTaper), 0.0, 1.0);
                    frequency = beginTaper + (nyquist - beginTaper) * (x + x * x - x * x * x);
                }
                return frequency;
            };
            const auto reference = effectiveFrequency(pivotFrequency);
            for (size_t bin = 0; bin < designSize; ++bin)
            {
                const auto frequency = static_cast<double>(std::min(bin, designSize - bin))
                    * sampleRate / static_cast<double>(designSize);
                const auto logGain = slope * std::log2(effectiveFrequency(frequency) / reference)
                    * (std::log(10.0) / 20.0);
                cepstrum[bin] = { logGain, 0.0 };
            }
            fft(cepstrum, true);
            for (size_t i = 1; i < designSize / 2; ++i)
                cepstrum[i] *= 2.0;
            for (size_t i = designSize / 2 + 1; i < designSize; ++i)
                cepstrum[i] = { 0.0, 0.0 };
            fft(cepstrum, false);
            for (auto& sample : cepstrum)
                sample = std::exp(sample);
            fft(cepstrum, true);
            for (size_t i = 0; i < tapCount; ++i)
            {
                // Gentle final taper suppresses artefacts from truncation.
                const auto fadeStart = tapCount * 3 / 4;
                const auto fade = i < fadeStart ? 1.0
                    : 0.5 + 0.5 * std::cos(pi * static_cast<double>(i - fadeStart)
                                           / static_cast<double>(tapCount - fadeStart));
                impulse[i] = cepstrum[i].real() * fade;
            }
            // A newly chosen SLOPE must not shift the 632-Hz pivot.
            std::complex<double> pivot { 0.0, 0.0 };
            for (size_t i = 0; i < tapCount; ++i)
            {
                const auto angle = -2.0 * pi * pivotFrequency / sampleRate * static_cast<double>(i);
                pivot += impulse[i] * std::complex<double> { std::cos(angle), std::sin(angle) };
            }
            const auto norm = 1.0 / std::max(1.0e-12, std::abs(pivot));
            for (auto& tap : impulse)
                tap *= norm;
        }
        for (size_t i = 0; i < blockSize; ++i)
            kernel->head[i] = static_cast<float>(impulse[i]);
        for (size_t partition = 1; partition < kernel->partitionCount; ++partition)
        {
            auto& spectrum = kernel->partitions[partition];
            spectrum.fill({ 0.0f, 0.0f });
            for (size_t i = 0; i < blockSize; ++i)
                spectrum[i] = { static_cast<float>(impulse[partition * blockSize + i]), 0.0f };
            transform(spectrum.data(), transformSize, false);
        }
        return kernel.release();
    }

    void processCompletedBlock() noexcept
    {
        if (asynchronous && retired.load(std::memory_order_acquire) == nullptr)
        {
            if (auto* ready = published.exchange(nullptr, std::memory_order_acq_rel))
            {
                auto* previous = activeKernel;
                activeKernel = ready;
                retired.store(previous, std::memory_order_release);
                // Any convolution overlap belongs to the previous FIR kernel.
                // Never append it to samples computed with newly installed taps.
                for (auto& channel : overlap)
                    channel.fill(0.0f);
                for (auto& channel : outputBlock)
                    channel.fill(0.0f);
                appliedKernelRevision.fetch_add(1, std::memory_order_release);
            }
        }
        if (activeKernel == nullptr)
            return;

        const auto slot = blockCursor % historyLength;
        for (size_t channel = 0; channel < 2; ++channel)
        {
            Spectrum block {};
            for (size_t i = 0; i < blockSize; ++i)
                block[i] = { inputBlock[channel][i], 0.0f };
            transform(block.data(), transformSize, false);
            inputSpectra[channel][slot] = block;
        }
        // The identity filter has no tail, so the expensive partitioned
        // convolution and inverse FFT are unnecessary. Forward FFTs above are
        // retained for accurate history if a nonzero kernel arrives later.
        if (activeKernel->slopeHundredth == 0)
        {
            ++blockCursor;
            return;
        }
        for (size_t channel = 0; channel < 2; ++channel)
        {
            Spectrum result {};
            for (size_t partition = 1; partition < activeKernel->partitionCount; ++partition)
            {
                // Current block m contributes via partition 1 to block m+1.
                const auto historySlot = (slot + historyLength - (partition - 1)) % historyLength;
                const auto& input = inputSpectra[channel][historySlot];
                const auto& filter = activeKernel->partitions[partition];
                for (size_t bin = 0; bin < transformSize; ++bin)
                    result[bin] += input[bin] * filter[bin];
            }
            transform(result.data(), transformSize, true);
            for (size_t i = 0; i < blockSize; ++i)
            {
                outputBlock[channel][i] = result[i].real() + overlap[channel][i];
                overlap[channel][i] = result[i + blockSize].real();
            }
        }
        ++blockCursor;
    }

    void runWorker()
    {
        // prepare() installs the default-slope kernel.  The audio thread may
        // request a different slope before this worker actually starts, so the
        // requested value is NOT a valid indication of what has been built.
        // Start from the slope of the kernel we know was installed.
        auto lastBuilt = static_cast<int>(std::lround(defaultSlopeDecibelsPerOctave * 100.0f));
        while (!stopping.load(std::memory_order_acquire))
        {
            if (auto* unused = retired.exchange(nullptr, std::memory_order_acq_rel))
                delete unused;
            const auto target = requestedSlopeHundredth.load(std::memory_order_acquire);
            if (target != lastBuilt)
            {
                auto* result = designKernel(sampleRate, static_cast<double>(target) / 100.0);
                if (stopping.load(std::memory_order_acquire))
                {
                    delete result;
                    break;
                }
                if (target != requestedSlopeHundredth.load(std::memory_order_acquire))
                {
                    delete result;
                    continue;
                }
                delete published.exchange(result, std::memory_order_acq_rel);
                lastBuilt = target;
            }
            std::unique_lock<std::mutex> lock(workerMutex);
            wakeWorker.wait_for(lock, std::chrono::milliseconds(20), [this, lastBuilt]
            {
                return stopping.load(std::memory_order_acquire)
                    || requestedSlopeHundredth.load(std::memory_order_acquire) != lastBuilt
                    || retired.load(std::memory_order_acquire) != nullptr;
            });
        }
        if (auto* unused = retired.exchange(nullptr, std::memory_order_acq_rel))
            delete unused;
    }

    void stopWorker()
    {
        stopping.store(true, std::memory_order_release);
        wakeWorker.notify_one();
        if (worker.joinable())
            worker.join();
        stopping.store(false, std::memory_order_relaxed);
    }

    double sampleRate = 44100.0;
    bool asynchronous = false;
    Kernel* activeKernel = nullptr; // owned by processing thread until retired
    std::atomic<Kernel*> published { nullptr };
    std::atomic<Kernel*> retired { nullptr };
    std::atomic<int> requestedSlopeHundredth { 0 };
    std::atomic<uint64_t> appliedKernelRevision { 0 };
    std::atomic<bool> stopping { false };
    std::mutex workerMutex;
    std::condition_variable wakeWorker;
    std::thread worker;

    std::array<std::array<float, blockSize>, 2> inputBlock {};
    std::array<std::array<float, blockSize>, 2> outputBlock {};
    std::array<std::array<float, blockSize>, 2> overlap {};
    std::array<std::vector<Spectrum>, 2> inputSpectra {};
    std::array<std::array<float, blockSize>, 2> headHistory {};
    size_t historyLength = 0;
    size_t blockCursor = 0;
    size_t inputIndex = 0;
};
}
