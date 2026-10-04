#pragma once

#include "offline/shell/AnalysisResult.h"
#include "shared/spec/FrequencyScale.h"
#include "shared/spec/SpectrogramFrequencyScale.h"
#include "shared/spec/SpectrumChannelLevels.h"

namespace ana::offline
{
struct SpectrogramMapWriter
{
    static constexpr float spectrogramFloorDb = -200.0f;
    using ChannelValues = std::array<float, offline::SpectrogramMap::channelCount>;

    struct Bucket
    {
        std::vector<float> stereoBins;
        std::array<std::vector<float>, offline::SpectrogramMap::channelCount> levels;
        uint32_t frameCount = 0;
    };

    explicit SpectrogramMapWriter(offline::SpectrogramMap& mapIn,
                                  const size_t requestedColumnCount)
        : map(mapIn),
          bucketCount(std::max<size_t>(1, requestedColumnCount))
    {
        buckets.resize(bucketCount);
    }

    size_t getColumnCount() const noexcept { return bucketCount; }

    void addTimeSpan(const double start, const double end)
    {
        if (end > start)
            map.timeSpans.push_back({ std::clamp(start, 0.0, 1.0),
                                      std::clamp(end, 0.0, 1.0) });
    }

    void addFrame(const fft::StereoFftFrame& frame,
                  const double sampleRate,
                  const double normalisedTime)
    {
        if (bucketCount == 0)
            return;

        const auto bucketIndex = std::min(bucketCount - 1,
            static_cast<size_t>(std::floor(
                juce::jlimit(0.0f, 1.0f, static_cast<float>(normalisedTime))
                    * static_cast<float>(bucketCount))));
        addFrameAtColumn(frame, sampleRate, bucketIndex);
    }

    void addFrameAtColumn(const fft::StereoFftFrame& frame,
                          const double sampleRate,
                          const size_t bucketIndex)
    {
        if (sampleRate <= 0.0 || frame.size <= 0 || bucketIndex >= bucketCount
            || map.rowCount == 0)
            return;

        const auto binCount = static_cast<size_t>(frame.size / 2 + 1);
        if (binCount < 2)
            return;

        if (map.sampleRate <= 0.0)
            map.sampleRate = sampleRate;
        map.fftSize = frame.size;
        map.binCount = binCount;

        auto& bucketPtr = buckets[bucketIndex];
        if (bucketPtr == nullptr)
        {
            bucketPtr = std::make_unique<Bucket>();
            bucketPtr->stereoBins.assign(binCount, spectrogramFloorDb);
            for (auto& channel : bucketPtr->levels)
                channel.assign(map.rowCount, spectrogramFloorDb);
        }
        auto& bucket = *bucketPtr;
        ++bucket.frameCount;

        // Match REALTIME SPEC channel definitions; MAP uses real-FFT magnitude scaled by 1/N.
        std::vector<ChannelValues> binLevels(binCount);
        for (size_t bin = 0; bin < binCount; ++bin)
        {
            const auto levels = fft::calculateStereoSpectrumLevels(
                frame, static_cast<int>(bin), 1.0f, spectrogramFloorDb);
            binLevels[bin] = ChannelValues {
                levels.stereoDecibels,
                levels.leftDecibels,
                levels.rightDecibels,
                levels.midDecibels,
                levels.sideDecibels
            };
        }

        // Preserve the strongest STFT frame when several land in one display column.
        for (size_t bin = 0; bin < binCount; ++bin)
        {
            // Keep one frequency grid even when items use different sample rates.
            const auto sourceBin = static_cast<double>(bin) * map.sampleRate / sampleRate;
            auto value = spectrogramFloorDb;
            if (sourceBin <= static_cast<double>(binCount - 1))
            {
                const auto first = static_cast<size_t>(std::floor(sourceBin));
                const auto second = std::min(binCount - 1, first + 1);
                const auto mix = static_cast<float>(sourceBin - static_cast<double>(first));
                value = binLevels[first][0] + mix * (binLevels[second][0] - binLevels[first][0]);
            }
            if (bucket.frameCount == 1)
                bucket.stereoBins[bin] = value;
            else
                bucket.stereoBins[bin] = std::max(bucket.stereoBins[bin], value);
        }

        const auto binFrequency = static_cast<float>(sampleRate)
            / static_cast<float>(frame.size);
        constexpr auto lowFrequency = frequency_scale::minimumHz;
        constexpr auto highFrequency = frequency_scale::maximumHz;
        for (size_t row = 0; row < map.rowCount; ++row)
        {
            const auto centreNormalised = 1.0f
                - (static_cast<float>(row) + 0.5f) / static_cast<float>(map.rowCount);
            const auto centreFrequency = spectrogram_frequency::frequencyAt(
                lowFrequency, highFrequency, centreNormalised);

            ChannelValues rowLevels {};
            rowLevels.fill(spectrogramFloorDb);
            if (binFrequency > 0.0f)
            {
                const auto exactBin = juce::jlimit(
                    1.0f, static_cast<float>(binCount - 1),
                    centreFrequency / binFrequency);
                const auto bin0 = static_cast<size_t>(std::floor(exactBin));
                const auto bin1 = std::min(binCount - 1, bin0 + 1);
                const auto mix = exactBin - static_cast<float>(bin0);
                for (size_t channel = 0; channel < rowLevels.size(); ++channel)
                    rowLevels[channel] = binLevels[bin0][channel]
                        + mix * (binLevels[bin1][channel] - binLevels[bin0][channel]);
            }

            for (size_t channel = 0; channel < bucket.levels.size(); ++channel)
            {
                if (bucket.frameCount == 1)
                    bucket.levels[channel][row] = rowLevels[channel];
                else
                    bucket.levels[channel][row] = std::max(
                        bucket.levels[channel][row], rowLevels[channel]);
            }
        }
    }

    void finish()
    {
        map.columnCount = bucketCount;
        map.columnFrameCounts.assign(map.columnCount, 0);

        const auto sampleCount = map.columnCount * map.rowCount;
        map.stereoDecibels.assign(map.columnCount * map.binCount, spectrogramFloorDb);
        for (auto& channel : map.levels)
            channel.assign(sampleCount, spectrogramFloorDb);

        for (size_t column = 0; column < map.columnCount; ++column)
        {
            if (buckets[column] == nullptr || buckets[column]->frameCount == 0)
                continue;

            const auto& bucket = *buckets[column];
            map.columnFrameCounts[column] = bucket.frameCount;
            if (bucket.stereoBins.size() == map.binCount)
                for (size_t bin = 0; bin < map.binCount; ++bin)
                    map.stereoDecibels[column * map.binCount + bin] = bucket.stereoBins[bin];
            for (size_t channel = 0; channel < map.levels.size(); ++channel)
                for (size_t row = 0; row < map.rowCount; ++row)
                    map.levels[channel][row * map.columnCount + column]
                        = bucket.levels[channel][row];
        }
    }

    offline::SpectrogramMap& map;
    size_t bucketCount = 0;
    std::vector<std::unique_ptr<Bucket>> buckets;
};

}

