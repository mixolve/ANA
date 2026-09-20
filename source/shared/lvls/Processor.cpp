#include "Processor.h"

#include <algorithm>
#include <cmath>

namespace ana::lvls
{
namespace
{
constexpr float historyStepSeconds = 0.1f;
constexpr float absoluteLoudnessGate = -70.0f;
constexpr float loudnessHistogramLow = -70.0f;
constexpr float loudnessHistogramStep = 0.1f;
constexpr float aes17RmsReferenceOffset = 3.01029995664f;
constexpr float midSideCompensationGain = 0.5f;
constexpr size_t loudnessRangeFrameStride = 10;

float decibels(const float gain) noexcept
{
    return juce::Decibels::gainToDecibels(std::max(gain, 1.0e-6f), LvlsProcessor::minimumDecibels);
}

float powerDecibels(const float power) noexcept
{
    return power > 1.0e-12f ? 10.0f * std::log10(power) : LvlsProcessor::minimumDecibels;
}

float lufsFromPower(const float power) noexcept
{
    return juce::jmax(LvlsProcessor::minimumDecibels, -0.691f + powerDecibels(power));
}

void configureHighShelf(LvlsProcessor::Biquad& filter,
                        const double sampleRate,
                        const double frequency,
                        const double quality,
                        const double gainDecibels) noexcept
{
    const auto k = std::tan(juce::MathConstants<double>::pi * frequency / sampleRate);
    const auto highFrequencyGain = std::pow(10.0, gainDecibels / 20.0);
    const auto transitionGain = std::pow(highFrequencyGain, 0.499666774155);
    const auto denominator = 1.0 + k / quality + k * k;

    filter.b0 = static_cast<float>((highFrequencyGain + transitionGain * k / quality + k * k)
                                   / denominator);
    filter.b1 = static_cast<float>(2.0 * (k * k - highFrequencyGain) / denominator);
    filter.b2 = static_cast<float>((highFrequencyGain - transitionGain * k / quality + k * k)
                                   / denominator);
    filter.a1 = static_cast<float>(2.0 * (k * k - 1.0) / denominator);
    filter.a2 = static_cast<float>((1.0 - k / quality + k * k) / denominator);
    filter.reset();
}

void configureHighPass(LvlsProcessor::Biquad& filter,
                       const double sampleRate,
                       const double frequency,
                       const double quality) noexcept
{
    const auto k = std::tan(juce::MathConstants<double>::pi * frequency / sampleRate);
    const auto denominator = 1.0 + k / quality + k * k;

    // BS.1770 specifies a unity-normalised double zero at DC for this stage.
    filter.b0 = 1.0f;
    filter.b1 = -2.0f;
    filter.b2 = 1.0f;
    filter.a1 = static_cast<float>(2.0 * (k * k - 1.0) / denominator);
    filter.a2 = static_cast<float>((1.0 - k / quality + k * k) / denominator);
    filter.reset();
}
}

float LvlsProcessor::Biquad::process(const float input) noexcept
{
    const auto output = b0 * input + z1;
    z1 = b1 * input - a1 * output + z2;
    z2 = b2 * input - a2 * output;
    return output;
}

void LvlsProcessor::prepare(const double newSampleRate, const int maximumBlockSamples) noexcept
{
    sampleRate = std::max(1.0, newSampleRate);
    loudnessFrameSampleCount = static_cast<uint32_t>(std::max(1.0, std::round(sampleRate * historyStepSeconds)));
    const auto maximumRmsWindowSamples = static_cast<size_t>(std::ceil(sampleRate * 3.0));
    for (auto& history : rmsPowerHistory)
        history.assign(maximumRmsWindowSamples, 0.0f);
    loudnessPowerHistory.assign(maximumRmsWindowSamples, 0.0f);
    momentaryWindowSamples = static_cast<size_t>(std::max(1.0, std::round(sampleRate * 0.4)));
    shortTermWindowSamples = static_cast<size_t>(std::max(1.0, std::round(sampleRate * 3.0)));
    const auto oversamplingStages = sampleRate < 96000.0 ? size_t { 2 }
        : sampleRate < 192000.0 ? size_t { 1 } : size_t { 0 };
    truePeakOversampler = std::make_unique<juce::dsp::Oversampling<float>>(
        2, oversamplingStages,
        juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
    truePeakMaximumInputBlockSamples = static_cast<size_t>(std::max(1, maximumBlockSamples));
    truePeakOversampler->initProcessing(truePeakMaximumInputBlockSamples);
    configureKWeighting();
    reset();
}

void LvlsProcessor::reset() noexcept
{
    for (auto& filter : highShelves)
        filter.reset();
    for (auto& filter : highPasses)
        filter.reset();
    if (truePeakOversampler != nullptr)
        truePeakOversampler->reset();
    for (auto& series : publishedHistory)
        for (auto& value : series)
            value.store(LvlsProcessor::minimumDecibels, std::memory_order_relaxed);
    for (auto& value : publishedPeakDecibels)
        value.store(LvlsProcessor::minimumDecibels, std::memory_order_relaxed);
    for (auto& value : publishedRmsDecibels)
        value.store(LvlsProcessor::minimumDecibels, std::memory_order_relaxed);
    for (auto& value : publishedPeakMaximumDecibels)
        value.store(LvlsProcessor::minimumDecibels, std::memory_order_relaxed);
    for (auto& value : publishedPeakHoldDecibels)
        value.store(LvlsProcessor::minimumDecibels, std::memory_order_relaxed);
    for (auto& value : publishedRmsMaximumDecibels)
        value.store(LvlsProcessor::minimumDecibels, std::memory_order_relaxed);
    publishedMomentaryLufs.store(LvlsProcessor::minimumDecibels, std::memory_order_relaxed);
    publishedShortTermLufs.store(LvlsProcessor::minimumDecibels, std::memory_order_relaxed);
    publishedIntegratedLufs.store(LvlsProcessor::minimumDecibels, std::memory_order_relaxed);
    publishedMomentaryMaximumLufs.store(LvlsProcessor::minimumDecibels, std::memory_order_relaxed);
    publishedShortTermMaximumLufs.store(LvlsProcessor::minimumDecibels, std::memory_order_relaxed);
    publishedIntegratedMaximumLufs.store(LvlsProcessor::minimumDecibels, std::memory_order_relaxed);
    publishedLoudnessRange.store(0.0f, std::memory_order_relaxed);
    historyWriteCount.store(0, std::memory_order_release);
    for (auto& history : rmsPowerHistory)
        std::fill(history.begin(), history.end(), 0.0f);
    rmsPowerSums.fill(0.0);
    std::fill(loudnessPowerHistory.begin(), loudnessPowerHistory.end(), 0.0f);
    peakGains.fill(0.0f);
    peakHoldGains.fill(0.0f);
    peakHoldSamplesRemaining.fill(0);
    rmsPowerWriteCount = 0;
    rmsWindowSamples = 1;
    loudnessPowerWriteCount = 0;
    momentaryPowerSum = 0.0;
    shortTermPowerSum = 0.0;
    momentaryLufsPower = 0.0f;
    shortTermLufsPower = 0.0f;
    integratedLoudnessEnergies.fill(0.0);
    integratedLoudnessCounts.fill(0);
    loudnessRangeEnergies.fill(0.0);
    loudnessRangeCounts.fill(0);
    integratedFrameWriteCount = 0;
    pendingLoudnessFrameSamples = 0;
    revision.fetch_add(1, std::memory_order_release);
}

void LvlsProcessor::requestClear() noexcept
{
    clearRequested.store(true, std::memory_order_release);

    for (auto& value : publishedPeakDecibels)
        value.store(LvlsProcessor::minimumDecibels, std::memory_order_release);
    for (auto& value : publishedRmsDecibels)
        value.store(LvlsProcessor::minimumDecibels, std::memory_order_release);
    for (auto& value : publishedPeakMaximumDecibels)
        value.store(LvlsProcessor::minimumDecibels, std::memory_order_release);
    for (auto& value : publishedPeakHoldDecibels)
        value.store(LvlsProcessor::minimumDecibels, std::memory_order_release);
    for (auto& value : publishedRmsMaximumDecibels)
        value.store(LvlsProcessor::minimumDecibels, std::memory_order_release);
    publishedMomentaryLufs.store(LvlsProcessor::minimumDecibels, std::memory_order_release);
    publishedShortTermLufs.store(LvlsProcessor::minimumDecibels, std::memory_order_release);
    publishedIntegratedLufs.store(LvlsProcessor::minimumDecibels, std::memory_order_release);
    publishedMomentaryMaximumLufs.store(LvlsProcessor::minimumDecibels, std::memory_order_release);
    publishedShortTermMaximumLufs.store(LvlsProcessor::minimumDecibels, std::memory_order_release);
    publishedIntegratedMaximumLufs.store(LvlsProcessor::minimumDecibels, std::memory_order_release);
    publishedLoudnessRange.store(0.0f, std::memory_order_release);
    historyWriteCount.store(0, std::memory_order_release);
    revision.fetch_add(1, std::memory_order_release);
}

void LvlsProcessor::setFrozen(const bool shouldFreeze) noexcept
{
    frozen.store(shouldFreeze, std::memory_order_release);
}

bool LvlsProcessor::isFrozen() const noexcept
{
    return frozen.load(std::memory_order_acquire);
}

void LvlsProcessor::processBlock(const juce::AudioBuffer<float>& buffer,
                                       const float rmsWindowMilliseconds,
                                       const float peakHoldMilliseconds,
                                       const bool holdOnTransportStop,
                                       const ProcessingOptions options) noexcept
{
    if (clearRequested.exchange(false, std::memory_order_acq_rel))
        reset();

    if (frozen.load(std::memory_order_acquire) || sampleRate <= 0.0
        || buffer.getNumSamples() <= 0 || buffer.getNumChannels() <= 0)
        return;

    const auto processPeakSignal = options.peakRms;
    const auto processLoudnessSignal = options.loudness || options.history;
    if (! processPeakSignal && ! processLoudnessSignal)
        return;

    if (holdOnTransportStop)
    {
        auto hasSignal = false;
        for (int channel = 0; channel < std::min(2, buffer.getNumChannels()); ++channel)
            hasSignal = hasSignal || buffer.getMagnitude(channel, 0, buffer.getNumSamples()) > 1.0e-7f;
        if (! hasSignal)
            return;
    }

    const auto sampleCount = buffer.getNumSamples();
    const auto channelCount = std::min(2, buffer.getNumChannels());
    std::array<float, 4> blockPeaks {};
    if (processPeakSignal && truePeakOversampler != nullptr)
    {
        // JUCE Oversampling has a fixed prepared block capacity; chunk oversized callbacks.
        const juce::dsp::AudioBlock<const float> inputBlock(buffer);
        size_t inputOffset = 0;
        const auto maximumChunk = std::max<size_t>(1, truePeakMaximumInputBlockSamples);
        while (inputOffset < inputBlock.getNumSamples())
        {
            const auto chunkSamples = std::min(maximumChunk,
                                               inputBlock.getNumSamples() - inputOffset);
            const auto inputChunk = inputBlock.getSubBlock(inputOffset, chunkSamples);
            const auto oversampled = truePeakOversampler->processSamplesUp(inputChunk);
            for (int channel = 0; channel < channelCount; ++channel)
            {
                const auto range = juce::FloatVectorOperations::findMinAndMax(
                    oversampled.getChannelPointer(static_cast<size_t>(channel)),
                    static_cast<int>(oversampled.getNumSamples()));
                blockPeaks[static_cast<size_t>(channel)] = std::max(
                    blockPeaks[static_cast<size_t>(channel)],
                    std::max(std::abs(range.getStart()), std::abs(range.getEnd())));
            }

            const auto* left = oversampled.getChannelPointer(0);
            const auto* right = channelCount > 1 ? oversampled.getChannelPointer(1) : left;
            for (size_t sample = 0; sample < oversampled.getNumSamples(); ++sample)
            {
                blockPeaks[2] = std::max(blockPeaks[2],
                    std::abs(midSideCompensationGain * (left[sample] + right[sample])));
                blockPeaks[3] = std::max(blockPeaks[3],
                    std::abs(midSideCompensationGain * (left[sample] - right[sample])));
            }

            inputOffset += chunkSamples;
        }
    }
    const auto maximumRmsWindowSamples = rmsPowerHistory[0].size();
    const auto requestedRmsWindowSamples = static_cast<size_t>(juce::jlimit(
        1, static_cast<int>(maximumRmsWindowSamples),
        juce::roundToInt(sampleRate * juce::jlimit(minimumRmsWindowMilliseconds, maximumRmsWindowMilliseconds,
                                          rmsWindowMilliseconds) * 0.001f)));
    if (options.peakRms && requestedRmsWindowSamples != rmsWindowSamples)
    {
        for (auto& history : rmsPowerHistory)
            std::fill(history.begin(), history.end(), 0.0f);
        rmsPowerSums.fill(0.0);
        rmsPowerWriteCount = 0;
        rmsWindowSamples = requestedRmsWindowSamples;
    }

    for (int sample = 0; sample < sampleCount; ++sample)
    {
        auto weightedSampleEnergy = 0.0;
        const auto left = buffer.getSample(0, sample);
        const auto right = channelCount > 1 ? buffer.getSample(1, sample) : left;
        const std::array<float, 4> lvlsSamples {
            left, right,
            midSideCompensationGain * (left + right),
            midSideCompensationGain * (left - right)
        };
        if (options.peakRms)
        {
            const auto historyIndex = rmsPowerWriteCount % maximumRmsWindowSamples;
            const auto hasFullRmsWindow = rmsPowerWriteCount >= rmsWindowSamples;
            const auto expiredIndex = (rmsPowerWriteCount + maximumRmsWindowSamples - rmsWindowSamples)
                % maximumRmsWindowSamples;
            for (size_t index = 0; index < lvlsSamples.size(); ++index)
            {
                const auto power = lvlsSamples[index] * lvlsSamples[index];
                const auto expiredPower = hasFullRmsWindow ? rmsPowerHistory[index][expiredIndex] : 0.0f;
                rmsPowerHistory[index][historyIndex] = power;
                rmsPowerSums[index] += static_cast<double>(power - expiredPower);
            }
        }
        if (processLoudnessSignal)
        {
            for (int channel = 0; channel < channelCount; ++channel)
            {
                const auto input = lvlsSamples[static_cast<size_t>(channel)];
                const auto weighted = highPasses[static_cast<size_t>(channel)].process(
                    highShelves[static_cast<size_t>(channel)].process(input));
                weightedSampleEnergy += static_cast<double>(weighted) * static_cast<double>(weighted);
            }

            const auto loudnessHistorySize = loudnessPowerHistory.size();
            const auto loudnessHistoryIndex = loudnessPowerWriteCount % loudnessHistorySize;
            const auto expiredMomentaryIndex = (loudnessPowerWriteCount + loudnessHistorySize
                - momentaryWindowSamples) % loudnessHistorySize;
            const auto expiredShortTermIndex = (loudnessPowerWriteCount + loudnessHistorySize
                - shortTermWindowSamples) % loudnessHistorySize;
            const auto expiredMomentaryPower = loudnessPowerWriteCount >= momentaryWindowSamples
                ? loudnessPowerHistory[expiredMomentaryIndex] : 0.0f;
            const auto expiredShortTermPower = loudnessPowerWriteCount >= shortTermWindowSamples
                ? loudnessPowerHistory[expiredShortTermIndex] : 0.0f;
            loudnessPowerHistory[loudnessHistoryIndex] = static_cast<float>(weightedSampleEnergy);
            momentaryPowerSum += weightedSampleEnergy - static_cast<double>(expiredMomentaryPower);
            shortTermPowerSum += weightedSampleEnergy - static_cast<double>(expiredShortTermPower);
            ++loudnessPowerWriteCount;

            ++pendingLoudnessFrameSamples;
            if (pendingLoudnessFrameSamples >= loudnessFrameSampleCount)
            {
                updateRollingLoudnessPowers();
                publishLoudnessFrame(options.loudness, options.history);
            }
        }
        if (options.peakRms)
            ++rmsPowerWriteCount;
    }
    if (processLoudnessSignal)
        updateRollingLoudnessPowers();

    if (processPeakSignal)
    {
        const auto durationSeconds = static_cast<float>(sampleCount / sampleRate);
        const auto peakRelease = std::pow(10.0f, -durationSeconds);
        const auto peakHoldSamples = static_cast<uint64_t>(std::max(
            0.0, std::round(sampleRate * juce::jlimit(minimumPeakHoldMilliseconds, maximumPeakHoldMilliseconds,
                                           peakHoldMilliseconds) * 0.001)));
        for (size_t index = 0; index < blockPeaks.size(); ++index)
        {
            peakGains[index] = std::max(blockPeaks[index], peakGains[index] * peakRelease);
            if (blockPeaks[index] >= peakHoldGains[index])
            {
                peakHoldGains[index] = blockPeaks[index];
                peakHoldSamplesRemaining[index] = peakHoldSamples;
            }
            else if (peakHoldSamplesRemaining[index] > static_cast<uint64_t>(sampleCount))
            {
                peakHoldSamplesRemaining[index] -= static_cast<uint64_t>(sampleCount);
            }
            else
            {
                peakHoldSamplesRemaining[index] = 0;
                peakHoldGains[index] = peakGains[index];
            }
        }

        for (size_t channel = 0; channel < blockPeaks.size(); ++channel)
        {
            const auto peakDecibels = decibels(peakGains[channel]);
            publishedPeakDecibels[channel].store(peakDecibels, std::memory_order_release);
            publishedPeakHoldDecibels[channel].store(decibels(peakHoldGains[channel]),
                                                      std::memory_order_release);
            publishedPeakMaximumDecibels[channel].store(std::max(
                peakDecibels, publishedPeakMaximumDecibels[channel].load(std::memory_order_relaxed)),
                std::memory_order_release);
            if (options.peakRms)
            {
                const auto rmsSampleCount = std::max<size_t>(1,
                    std::min(rmsPowerWriteCount, rmsWindowSamples));
                // AES-17 RMS reference: a full-scale sine reads 0 dB.
                const auto rmsDecibels = juce::jmax(LvlsProcessor::minimumDecibels,
                    powerDecibels(static_cast<float>(rmsPowerSums[channel]
                        / static_cast<double>(rmsSampleCount))) + aes17RmsReferenceOffset);
                publishedRmsDecibels[channel].store(rmsDecibels, std::memory_order_release);
                publishedRmsMaximumDecibels[channel].store(std::max(
                    rmsDecibels, publishedRmsMaximumDecibels[channel].load(std::memory_order_relaxed)),
                    std::memory_order_release);
            }
        }
    }
    if (options.loudness)
    {
        const auto momentaryLufs = lufsFromPower(momentaryLufsPower);
        const auto shortTermLufs = lufsFromPower(shortTermLufsPower);
        const auto integratedLufs = getIntegratedLufs();
        publishedMomentaryLufs.store(momentaryLufs, std::memory_order_release);
        publishedShortTermLufs.store(shortTermLufs, std::memory_order_release);
        publishedIntegratedLufs.store(integratedLufs, std::memory_order_release);
        if (loudnessPowerWriteCount >= momentaryWindowSamples)
            publishedMomentaryMaximumLufs.store(std::max(
                momentaryLufs, publishedMomentaryMaximumLufs.load(std::memory_order_relaxed)),
                std::memory_order_release);
        if (loudnessPowerWriteCount >= shortTermWindowSamples)
            publishedShortTermMaximumLufs.store(std::max(
                shortTermLufs, publishedShortTermMaximumLufs.load(std::memory_order_relaxed)),
                std::memory_order_release);
        if (integratedLufs > LvlsProcessor::minimumDecibels)
            publishedIntegratedMaximumLufs.store(std::max(
                integratedLufs, publishedIntegratedMaximumLufs.load(std::memory_order_relaxed)),
                std::memory_order_release);
    }
    if (options.history)
        publishedLoudnessRange.store(getLoudnessRange(), std::memory_order_release);

    revision.fetch_add(1, std::memory_order_release);
}

LvlsProcessor::Values LvlsProcessor::getValues() const noexcept
{
    Values values;
    for (size_t index = 0; index < values.peakDecibels.size(); ++index)
    {
        values.peakDecibels[index] = publishedPeakDecibels[index].load(std::memory_order_acquire);
        values.rmsDecibels[index] = publishedRmsDecibels[index].load(std::memory_order_acquire);
        values.peakMaximumDecibels[index] = publishedPeakMaximumDecibels[index].load(std::memory_order_acquire);
        values.peakHoldDecibels[index] = publishedPeakHoldDecibels[index].load(std::memory_order_acquire);
        values.rmsMaximumDecibels[index] = publishedRmsMaximumDecibels[index].load(std::memory_order_acquire);
    }
    values.momentaryLufs = publishedMomentaryLufs.load(std::memory_order_acquire);
    values.shortTermLufs = publishedShortTermLufs.load(std::memory_order_acquire);
    values.integratedLufs = publishedIntegratedLufs.load(std::memory_order_acquire);
    values.momentaryMaximumLufs = publishedMomentaryMaximumLufs.load(std::memory_order_acquire);
    values.shortTermMaximumLufs = publishedShortTermMaximumLufs.load(std::memory_order_acquire);
    values.integratedMaximumLufs = publishedIntegratedMaximumLufs.load(std::memory_order_acquire);
    values.loudnessRange = publishedLoudnessRange.load(std::memory_order_acquire);
    return values;
}

void LvlsProcessor::copyHistory(const size_t series, std::vector<float>& destination) const
{
    const auto writeCount = historyWriteCount.load(std::memory_order_acquire);
    const auto count = std::min(writeCount, historyCapacity);
    destination.resize(count);
    const auto first = writeCount > historyCapacity ? writeCount - historyCapacity : 0;
    const auto seriesIndex = std::min(series, historySeriesCount - 1);
    for (size_t index = 0; index < count; ++index)
        destination[index] = publishedHistory[seriesIndex][(first + index) % historyCapacity]
            .load(std::memory_order_acquire);
}

uint64_t LvlsProcessor::getRevision() const noexcept
{
    return revision.load(std::memory_order_acquire);
}

void LvlsProcessor::configureKWeighting() noexcept
{
    for (auto& filter : highShelves)
        configureHighShelf(filter, sampleRate, 1681.97445095553, 0.7071752369554196, 3.999843853973347);
    for (auto& filter : highPasses)
        configureHighPass(filter, sampleRate, 38.13547087602444, 0.5003270373238773);
}

void LvlsProcessor::updateRollingLoudnessPowers() noexcept
{
    momentaryLufsPower = loudnessPowerWriteCount >= momentaryWindowSamples
        ? static_cast<float>(momentaryPowerSum / static_cast<double>(momentaryWindowSamples))
        : 0.0f;
    shortTermLufsPower = loudnessPowerWriteCount >= shortTermWindowSamples
        ? static_cast<float>(shortTermPowerSum / static_cast<double>(shortTermWindowSamples))
        : 0.0f;
}

void LvlsProcessor::publishLoudnessFrame(const bool includeLoudness,
                                               const bool includeHistory) noexcept
{
    if (pendingLoudnessFrameSamples == 0)
        return;

    ++integratedFrameWriteCount;
    if ((includeLoudness || includeHistory) && loudnessPowerWriteCount >= momentaryWindowSamples)
        addLoudnessBlock(momentaryLufsPower,
                         integratedLoudnessEnergies,
                         integratedLoudnessCounts);

    if (includeHistory)
    {
        publishHistoryValues({
            loudnessPowerWriteCount >= momentaryWindowSamples
                ? lufsFromPower(momentaryLufsPower) : LvlsProcessor::minimumDecibels,
            loudnessPowerWriteCount >= shortTermWindowSamples
                ? lufsFromPower(shortTermLufsPower) : LvlsProcessor::minimumDecibels,
            getIntegratedLufs()
        });
    }

    if (loudnessPowerWriteCount >= shortTermWindowSamples)
    {
        if (includeHistory && integratedFrameWriteCount % loudnessRangeFrameStride == 0)
            addLoudnessBlock(shortTermLufsPower,
                             loudnessRangeEnergies,
                             loudnessRangeCounts);
    }
    pendingLoudnessFrameSamples = 0;
}

void LvlsProcessor::addLoudnessBlock(
    const float power,
    std::array<double, loudnessHistogramBinCount>& energies,
    std::array<uint64_t, loudnessHistogramBinCount>& counts) noexcept
{
    const auto loudness = lufsFromPower(power);
    if (loudness < absoluteLoudnessGate)
        return;

    const auto index = juce::jlimit(0, static_cast<int>(loudnessHistogramBinCount) - 1,
        juce::roundToInt((loudness - loudnessHistogramLow) / loudnessHistogramStep));
    energies[static_cast<size_t>(index)] += power;
    ++counts[static_cast<size_t>(index)];
}

float LvlsProcessor::getIntegratedLufs() const noexcept
{
    auto energy = 0.0;
    uint64_t count = 0;
    for (size_t index = 0; index < loudnessHistogramBinCount; ++index)
    {
        energy += integratedLoudnessEnergies[index];
        count += integratedLoudnessCounts[index];
    }

    if (count == 0)
        return LvlsProcessor::minimumDecibels;

    const auto ungatedPower = static_cast<float>(energy / static_cast<double>(count));
    const auto relativeGate = std::max(absoluteLoudnessGate, lufsFromPower(ungatedPower) - 10.0f);
    const auto firstIndex = juce::jlimit(0, static_cast<int>(loudnessHistogramBinCount) - 1,
        static_cast<int>(std::ceil((relativeGate - loudnessHistogramLow) / loudnessHistogramStep)));
    energy = 0.0;
    count = 0;
    for (auto index = firstIndex; index < static_cast<int>(loudnessHistogramBinCount); ++index)
    {
        energy += integratedLoudnessEnergies[static_cast<size_t>(index)];
        count += integratedLoudnessCounts[static_cast<size_t>(index)];
    }

    return count > 0 ? lufsFromPower(static_cast<float>(energy / static_cast<double>(count)))
                     : LvlsProcessor::minimumDecibels;
}

float LvlsProcessor::getLoudnessRange() const noexcept
{
    auto energy = 0.0;
    uint64_t count = 0;
    for (size_t index = 0; index < loudnessHistogramBinCount; ++index)
    {
        energy += loudnessRangeEnergies[index];
        count += loudnessRangeCounts[index];
    }

    if (count == 0)
        return 0.0f;

    const auto relativeGate = std::max(absoluteLoudnessGate,
        lufsFromPower(static_cast<float>(energy / static_cast<double>(count))) - 20.0f);
    const auto firstIndex = juce::jlimit(0, static_cast<int>(loudnessHistogramBinCount) - 1,
        static_cast<int>(std::ceil((relativeGate - loudnessHistogramLow) / loudnessHistogramStep)));
    count = 0;
    for (auto index = firstIndex; index < static_cast<int>(loudnessHistogramBinCount); ++index)
        count += loudnessRangeCounts[static_cast<size_t>(index)];
    if (count == 0)
        return 0.0f;

    const auto percentileIndex = [&] (const double percentile)
    {
        const auto target = static_cast<uint64_t>(std::floor(
            percentile * static_cast<double>(count - 1) + 0.5));
        uint64_t cumulative = 0;
        for (auto index = firstIndex; index < static_cast<int>(loudnessHistogramBinCount); ++index)
        {
            cumulative += loudnessRangeCounts[static_cast<size_t>(index)];
            if (cumulative > target)
                return index;
        }
        return static_cast<int>(loudnessHistogramBinCount) - 1;
    };

    const auto lowIndex = percentileIndex(0.10);
    const auto highIndex = percentileIndex(0.95);
    return std::max(0.0f, static_cast<float>(highIndex - lowIndex) * loudnessHistogramStep);
}

void LvlsProcessor::publishHistoryValues(
    const std::array<float, historySeriesCount>& values) noexcept
{
    const auto writeCount = historyWriteCount.fetch_add(1, std::memory_order_acq_rel);
    for (size_t series = 0; series < historySeriesCount; ++series)
        publishedHistory[series][writeCount % historyCapacity]
            .store(values[series], std::memory_order_release);
}
}
