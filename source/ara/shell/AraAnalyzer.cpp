#include "AraAnalyzer.h"
#include "AraSourceCatalog.h"

#include "corr/Processor.h"
#include "shared/scop/AnalysisTypes.h"
#include "shared/analyzer/Frequency.h"
#include "shared/analyzer/SpectrumChannelLevels.h"
#include "shared/analyzer/SpectrogramFrequencyScale.h"
#include "spec/Processor.h"
#include "shared/lvls/Processor.h"

#if JucePlugin_Enable_ARA

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

namespace ana::ara
{
namespace
{
constexpr int readBlockSize = 4096;

void accumulateEnvelope(scop::AnalysisChannelEnvelopes& envelopes,
                        const size_t column,
                        const scop::AnalysisChannelSamples& samples) noexcept
{
    for (size_t modeIndex = 0; modeIndex < samples.size(); ++modeIndex)
    {
        auto& envelope = envelopes[modeIndex];
        envelope.minimums[column] = std::min(envelope.minimums[column], samples[modeIndex]);
        envelope.maximums[column] = std::max(envelope.maximums[column], samples[modeIndex]);
    }
}

void accumulateScopSample(ara::AnalysisResult& result,
                           dsp::LinkwitzRileyCrossover& crossover,
                           const size_t column,
                           const float left,
                           const float right) noexcept
{
    accumulateEnvelope(result.wideband, column, scop::makeAnalysisChannelSamples(left, right));

    const auto bands = crossover.processSample(left, right);
    for (size_t bandIndex = 0; bandIndex < result.activeBandCount; ++bandIndex)
    {
        const auto bandLeft = static_cast<float>(bands[bandIndex].left);
        const auto bandRight = static_cast<float>(bands[bandIndex].right);
        accumulateEnvelope(result.bands[bandIndex], column,
                           scop::makeAnalysisChannelSamples(bandLeft, bandRight));
    }
}

constexpr float spectrogramFloorDb = -200.0f;

int mapHopSizeForRaster(const double sourceSamplesPerPlaybackSecond,
                          const double timelineDurationSeconds,
                          const size_t rasterColumnCount,
                          const float timeOverlapFraction) noexcept
{
    if (sourceSamplesPerPlaybackSecond <= 0.0 || timelineDurationSeconds <= 0.0
        || rasterColumnCount == 0)
        return 1;

    // Time Overlap oversamples the display raster; NONE is one STFT sample per column.
    const auto baseHop = sourceSamplesPerPlaybackSecond * timelineDurationSeconds
        / static_cast<double>(rasterColumnCount);
    const auto oversamplingScale = 1.0 - static_cast<double>(
        juce::jlimit(0.0f, 0.9375f, timeOverlapFraction));
    return std::max(1, static_cast<int>(std::llround(baseHop * oversamplingScale)));
}

struct SpectrogramMapWriter
{
    using ChannelValues = std::array<float, ara::SpectrogramMap::channelCount>;

    struct Bucket
    {
        std::vector<float> stereoBins;
        std::array<std::vector<float>, ara::SpectrogramMap::channelCount> levels;
        uint32_t frameCount = 0;
    };

    explicit SpectrogramMapWriter(ara::SpectrogramMap& mapIn,
                                  const size_t requestedColumnCount)
        : map(mapIn),
          bucketCount(std::max<size_t>(1, requestedColumnCount))
    {
        buckets.resize(bucketCount);
    }

    size_t getColumnCount() const noexcept { return bucketCount; }

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

        // Match RTM SPEC channel definitions; MAP uses real-FFT magnitude scaled by 1/N.
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
            const auto value = binLevels[bin][0];
            if (bucket.frameCount == 1)
                bucket.stereoBins[bin] = value;
            else
                bucket.stereoBins[bin] = std::max(bucket.stereoBins[bin], value);
        }

        const auto binFrequency = static_cast<float>(sampleRate)
            / static_cast<float>(frame.size);
        constexpr auto lowFrequency = analyzer_frequency::minimumHz;
        constexpr auto highFrequency = analyzer_frequency::maximumHz;
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

    ara::SpectrogramMap& map;
    size_t bucketCount = 0;
    std::vector<std::unique_ptr<Bucket>> buckets;
};
}

static void prepareSpec(ara::AnalysisResult& result,
                        const int fftSize,
                        const float fftOverlap,
                        const float mapTimeOverlapFraction,
                        const bool mapMode,
                        const size_t mapColumnCount,
                        const size_t mapRowCount)
{
    result.spec = std::make_shared<spec::SpecProcessor>();
    result.specSampleRate = 0.0;
    result.specFftSize = fftSize;
    result.specFftOverlap = fftOverlap;
    result.specMapTimeOverlapFraction = mapTimeOverlapFraction;

    if (mapMode)
    {
        result.specMap = std::make_shared<ara::SpectrogramMap>();
        result.specMap->fftSize = fftSize;
        result.specMap->columnCount = std::max<size_t>(1, mapColumnCount);
        result.specMap->rowCount = std::clamp<size_t>(
            mapRowCount,
            ara::SpectrogramMap::minimumRowCount,
            ara::SpectrogramMap::maximumRowCount);
    }
}

static void processSpecBlock(ara::AnalysisResult& result,
                             juce::AudioBuffer<float>& buffer,
                             const int samplesToProcess,
                             const double sampleRate)
{
    if (result.spec == nullptr || sampleRate <= 0.0 || samplesToProcess <= 0)
        return;

    if (result.specSampleRate <= 0.0)
    {
        result.specSampleRate = sampleRate;
        result.spec->prepare(sampleRate);
    }

    if (! juce::approximatelyEqual(result.specSampleRate, sampleRate))
        return;

    result.spec->processAraBlock(buffer, result.specFftSize, result.specFftOverlap);
}

static void prepareCorr(ara::AnalysisResult& result,
                                       const int fftSize,
                                       const float fftOverlap,
                                       const int mode)
{
    result.corr = std::make_shared<corr::CorrProcessor>();
    result.corrSampleRate = 0.0;
    result.corrFftSize = fftSize;
    result.corrFftOverlap = fftOverlap;
    result.corrMode = juce::jlimit(0, corr::CorrProcessor::modeCount - 1, mode);
}

static void processCorrBlock(ara::AnalysisResult& result,
                                    juce::AudioBuffer<float>& buffer,
                                    const int samplesToProcess,
                                    const double sampleRate)
{
    if (result.corr == nullptr || sampleRate <= 0.0 || samplesToProcess <= 0)
        return;

    if (result.corrSampleRate <= 0.0)
    {
        result.corrSampleRate = sampleRate;
        result.corr->prepare(sampleRate);
    }

    if (! juce::approximatelyEqual(result.corrSampleRate, sampleRate))
        return;

    const auto corrMode = corr::CorrProcessor::modeFromIndex(result.corrMode);
    result.corr->processAraBlock(buffer, result.corrFftSize,
                                              result.corrFftOverlap,
                                              corrMode);
}

static void prepareLvls(ara::AnalysisResult& result)
{
    result.lvls = std::make_shared<lvls::LvlsProcessor>();
    result.lvlsSampleRate = 0.0;
}

struct LvlsConfig
{
    lvls::LvlsProcessor::ProcessingOptions options;
    float rmsWindowMs = 300.0f;
    float peakHoldMs = 1000.0f;
};

static void processLvlsBlock(ara::AnalysisResult& result,
                              juce::AudioBuffer<float>& buffer,
                              const int samplesToProcess,
                              const double sampleRate,
                              const LvlsConfig& config)
{
    if (result.lvls == nullptr || sampleRate <= 0.0 || samplesToProcess <= 0)
        return;

    if (result.lvlsSampleRate <= 0.0)
    {
        result.lvlsSampleRate = sampleRate;
        result.lvls->prepare(sampleRate);
    }

    if (! juce::approximatelyEqual(result.lvlsSampleRate, sampleRate))
        return;

    result.lvls->processBlock(buffer, config.rmsWindowMs, config.peakHoldMs, false, config.options);
}

static void processBlock(
    ara::AnalysisResult& result,
    juce::AudioBuffer<float>& buffer,
    const int samplesToProcess,
    const double sampleRate,
    const bool includeSpec,
    const bool includeCorr,
    const bool includeLvls,
    const LvlsConfig& lvlsConfig)
{
    if (samplesToProcess <= 0 || sampleRate <= 0.0
        || (! includeSpec && ! includeCorr && ! includeLvls))
        return;

    const auto validSamples = std::min(samplesToProcess, buffer.getNumSamples());
    if (validSamples <= 0)
        return;

    auto processingBuffer = juce::AudioBuffer<float>(
        buffer.getArrayOfWritePointers(), buffer.getNumChannels(), validSamples);

    if (includeSpec)
        processSpecBlock(result, processingBuffer, validSamples, sampleRate);
    if (includeCorr)
        processCorrBlock(result, processingBuffer, validSamples, sampleRate);
    if (includeLvls)
        processLvlsBlock(result, processingBuffer, validSamples, sampleRate, lvlsConfig);
}

template <typename PlaybackTimeForSourceSample>
static bool analyseMapSourceSegmentDirect(
    ara::AnalysisResult& result,
    juce::ARAAudioSource& source,
    const juce::int64 validSourceStart,
    const juce::int64 validSourceEnd,
    const double playbackStart,
    const double playbackEnd,
    const double gain,
    SpectrogramMapWriter& writer,
    PlaybackTimeForSourceSample&& playbackTimeForSourceSample,
    const std::function<bool()>& shouldCancel,
    const std::function<void(float)>& onProgress)
{
    if (result.spec == nullptr || result.specFftSize <= 0
        || result.durationSeconds <= 0.0 || writer.getColumnCount() == 0)
        return true;

    const auto sourceRate = source.getSampleRate();
    const auto sourceSampleCount = source.getSampleCount();
    if (sourceRate <= 0.0 || sourceSampleCount <= 0 || validSourceEnd <= validSourceStart
        || playbackEnd <= playbackStart)
        return true;

    // A result has one native FFT grid, so all contributing sources must share a sample rate.
    if (result.specSampleRate <= 0.0)
    {
        result.specSampleRate = sourceRate;
        result.spec->prepare(sourceRate);
    }
    if (! juce::approximatelyEqual(result.specSampleRate, sourceRate))
        return true;

    juce::ARAAudioSourceReader reader(&source);
    juce::AudioBuffer<float> readBuffer(2, readBlockSize);
    const auto sourceLength = validSourceEnd - validSourceStart;

    // Keep STFT state continuous across ARA read blocks; frame time comes from sampleOffset.
    for (auto readPosition = validSourceStart;
         readPosition < validSourceEnd;
         readPosition += readBlockSize)
    {
        if (shouldCancel())
            return false;

        const auto samplesToRead = static_cast<int>(std::min<juce::int64>(
            readBlockSize, validSourceEnd - readPosition));
        if (! reader.read(&readBuffer, 0, samplesToRead, readPosition, true, true))
            return false;

        if (! juce::approximatelyEqual(gain, 1.0))
            readBuffer.applyGain(0, samplesToRead, static_cast<float>(gain));

        const auto playbackBlockStart = playbackTimeForSourceSample(
            static_cast<double>(readPosition));
        const auto playbackBlockEnd = playbackTimeForSourceSample(
            static_cast<double>(readPosition + samplesToRead));
        const auto normalisedBlockStart = (playbackBlockStart - result.startTimeSeconds)
            / result.durationSeconds;
        const auto normalisedBlockEnd = (playbackBlockEnd - result.startTimeSeconds)
            / result.durationSeconds;

        const auto sourceSamplesPerPlaybackSecond = static_cast<double>(validSourceEnd - validSourceStart)
            / (playbackEnd - playbackStart);
        const auto mapHopSize = mapHopSizeForRaster(
            sourceSamplesPerPlaybackSecond, result.durationSeconds,
            writer.getColumnCount(), result.specMapTimeOverlapFraction);

        auto processingBuffer = juce::AudioBuffer<float>(
            readBuffer.getArrayOfWritePointers(), readBuffer.getNumChannels(), samplesToRead);
        result.spec->processAraMapBlock(
            processingBuffer, result.specFftSize, mapHopSize,
            [&writer, sourceRate, samplesToRead, normalisedBlockStart, normalisedBlockEnd]
            (const fft::StereoFftFrame& frame)
            {
                const auto centreOffset = static_cast<double>(frame.sampleOffset)
                    - 0.5 * static_cast<double>(frame.size);
                const auto blockFraction = samplesToRead > 0
                    ? centreOffset / static_cast<double>(samplesToRead) : 0.0;
                const auto normalisedTime = normalisedBlockStart
                    + blockFraction * (normalisedBlockEnd - normalisedBlockStart);
                writer.addFrame(frame, sourceRate, normalisedTime);
            });

        onProgress(static_cast<float>(readPosition + samplesToRead - validSourceStart)
                   / static_cast<float>(sourceLength));
    }

    return true;
}

static bool analyseTakeSource(
    ara::AnalysisResult& result,
    juce::ARAAudioSource& source,
    const dsp::LinkwitzRileyCrossover::CrossoverFrequencies& frequencies,
    const size_t crossoverCount,
    const bool includeScop,
    const bool includeSpec,
    const bool includeCorr,
    const bool includeLvls,
    const LvlsConfig& lvlsConfig,
    SpectrogramMapWriter* specMapWriter,
    const std::function<bool()>& shouldCancel,
    const std::function<void(float)>& onProgress)
{
    const auto sourceRate = source.getSampleRate();
    const auto sourceSampleCount = source.getSampleCount();
    const auto columnCount = includeScop
        ? result.bands.front().front().minimums.size() : size_t { 1 };

    if (sourceRate <= 0.0 || sourceSampleCount <= 0 || columnCount == 0)
        return true;

    if (specMapWriter != nullptr)
    {
        if (! analyseMapSourceSegmentDirect(
                result, source, 0, sourceSampleCount,
                result.startTimeSeconds, result.startTimeSeconds + result.durationSeconds,
                1.0, *specMapWriter,
                [&result, sourceRate] (const double sourceSample)
                {
                    return result.startTimeSeconds + sourceSample / sourceRate;
                },
                shouldCancel, onProgress))
            return false;

        if (! includeScop && ! includeCorr && ! includeLvls)
            return true;
    }

    if (includeSpec && specMapWriter == nullptr && result.spec != nullptr)
        result.spec->resetAraStream();

    juce::ARAAudioSourceReader reader(&source);
    juce::AudioBuffer<float> readBuffer(2, readBlockSize);
    dsp::LinkwitzRileyCrossover crossover;
    crossover.prepare(sourceRate);
    crossover.setCrossoverCount(crossoverCount);
    crossover.setCrossoverFrequencies(frequencies);
    for (juce::int64 readPosition = 0;
         readPosition < sourceSampleCount;
         readPosition += readBlockSize)
    {
        if (shouldCancel())
            return false;

        const auto samplesToRead = static_cast<int>(std::min<juce::int64>(
            readBlockSize, sourceSampleCount - readPosition));
        if (! reader.read(&readBuffer, 0, samplesToRead, readPosition, true, true))
            continue;

        processBlock(result, readBuffer, samplesToRead, sourceRate,
                     includeSpec && specMapWriter == nullptr, includeCorr, includeLvls, lvlsConfig);
        onProgress(static_cast<float>(readPosition + samplesToRead)
                   / static_cast<float>(sourceSampleCount));

        if (! includeScop)
            continue;

        const auto* left = readBuffer.getReadPointer(0);
        const auto* right = readBuffer.getReadPointer(1);

        for (int sampleIndex = 0; sampleIndex < samplesToRead; ++sampleIndex)
        {
            const auto normalizedTime = static_cast<double>(readPosition + sampleIndex)
                / static_cast<double>(sourceSampleCount);
            const auto column = std::min(columnCount - 1,
                static_cast<size_t>(normalizedTime * static_cast<double>(columnCount)));
            accumulateScopSample(result, crossover, column, left[sampleIndex], right[sampleIndex]);
        }
    }

    return true;
}

static void initialiseEnvelopes(ara::AnalysisResult& result, const size_t columnCount)
{
    const auto initialise = [columnCount] (auto& envelopes)
    {
        for (auto& envelope : envelopes)
        {
            envelope.minimums.assign(columnCount, std::numeric_limits<float>::max());
            envelope.maximums.assign(columnCount, std::numeric_limits<float>::lowest());
        }
    };

    for (auto& band : result.bands)
        initialise(band);
    initialise(result.wideband);
}

static void finishEnvelopes(ara::AnalysisResult& result)
{
    const auto finish = [] (auto& envelopes)
    {
        for (auto& envelope : envelopes)
        {
            for (size_t column = 0; column < envelope.minimums.size(); ++column)
            {
                if (envelope.minimums[column] > envelope.maximums[column])
                    envelope.minimums[column] = envelope.maximums[column] = 0.0f;
            }
        }
    };

    for (auto& band : result.bands)
        finish(band);
    finish(result.wideband);
}

static bool analyseHostTakeChoice(
    ara::AnalysisResult& result,
    juce::ARAAudioSource& source,
    const ara::SourceChoice& choice,
    const dsp::LinkwitzRileyCrossover::CrossoverFrequencies& frequencies,
    const size_t crossoverCount,
    const bool includeScop,
    const bool includeSpec,
    const bool includeCorr,
    const bool includeLvls,
    const LvlsConfig& lvlsConfig,
    SpectrogramMapWriter* specMapWriter,
    const std::function<bool()>& shouldCancel,
    const std::function<void(float)>& onProgress)
{
    const auto sourceRate = source.getSampleRate();
    const auto sourceSampleCount = source.getSampleCount();
    const auto columnCount = includeScop
        ? result.bands.front().front().minimums.size() : size_t { 1 };
    if (sourceRate <= 0.0 || sourceSampleCount <= 0 || columnCount == 0
        || choice.playbackDurationSeconds <= 0.0)
        return true;

    const auto requestedSourceStart = static_cast<juce::int64>(std::llround(
        choice.sourceStartSeconds * sourceRate));
    const auto wantedSourceLength = static_cast<juce::int64>(std::ceil(
        choice.playbackDurationSeconds * choice.playRate * sourceRate));
    const auto sourceStart = std::clamp<juce::int64>(
        requestedSourceStart, 0, sourceSampleCount);
    const auto sourceEnd = std::clamp<juce::int64>(
        requestedSourceStart + wantedSourceLength, 0, sourceSampleCount);
    if (sourceEnd <= sourceStart)
        return true;

    if (specMapWriter != nullptr)
    {
        const auto sourceSamplesPerPlaybackSecond = choice.playRate * sourceRate;
        if (! analyseMapSourceSegmentDirect(
                result, source, sourceStart, sourceEnd,
                choice.playbackStartSeconds,
                choice.playbackStartSeconds + choice.playbackDurationSeconds,
                // Analyze source audio only; applying REAPER D_VOL here would add an unmatched timeline gain.
                1.0, *specMapWriter,
                [&choice, sourceStart, sourceSamplesPerPlaybackSecond] (const double sourceSample)
                {
                    return choice.playbackStartSeconds
                        + (sourceSample - static_cast<double>(sourceStart))
                            / sourceSamplesPerPlaybackSecond;
                },
                shouldCancel, onProgress))
            return false;

        if (! includeScop && ! includeCorr && ! includeLvls)
            return true;
    }

    if (includeSpec && specMapWriter == nullptr && result.spec != nullptr)
        result.spec->resetAraStream();

    juce::ARAAudioSourceReader reader(&source);
    juce::AudioBuffer<float> readBuffer(2, readBlockSize);
    dsp::LinkwitzRileyCrossover crossover;
    crossover.prepare(sourceRate);
    crossover.setCrossoverCount(crossoverCount);
    crossover.setCrossoverFrequencies(frequencies);
    const auto sourceSamplesPerPlaybackSecond = choice.playRate * sourceRate;
    for (auto readPosition = sourceStart; readPosition < sourceEnd; readPosition += readBlockSize)
    {
        if (shouldCancel())
            return false;

        const auto samplesToRead = static_cast<int>(std::min<juce::int64>(
            readBlockSize, sourceEnd - readPosition));
        if (! reader.read(&readBuffer, 0, samplesToRead, readPosition, true, true))
            continue;

        if (! juce::approximatelyEqual(choice.gain, 1.0))
            readBuffer.applyGain(0, samplesToRead, static_cast<float>(choice.gain));

        processBlock(result, readBuffer, samplesToRead, sourceRate,
                     includeSpec && specMapWriter == nullptr, includeCorr, includeLvls, lvlsConfig);
        onProgress(static_cast<float>(readPosition + samplesToRead - sourceStart)
                   / static_cast<float>(sourceEnd - sourceStart));

        if (! includeScop)
            continue;

        const auto* left = readBuffer.getReadPointer(0);
        const auto* right = readBuffer.getReadPointer(1);
        for (int sampleIndex = 0; sampleIndex < samplesToRead; ++sampleIndex)
        {
            const auto sourceOffset = static_cast<double>(
                readPosition + sampleIndex - requestedSourceStart);
            const auto playbackTime = choice.playbackStartSeconds
                + sourceOffset / sourceSamplesPerPlaybackSecond;
            const auto normalizedTime = result.durationSeconds > 0.0
                ? (playbackTime - result.startTimeSeconds) / result.durationSeconds
                : 0.0;
            const auto column = std::min(columnCount - 1,
                static_cast<size_t>(std::max(0.0, normalizedTime)
                                    * static_cast<double>(columnCount)));
            accumulateScopSample(result, crossover, column, left[sampleIndex], right[sampleIndex]);
        }
    }

    return true;
}

static bool analyseHostTakeChoices(
    ara::AnalysisResult& result,
    ARA::PlugIn::DocumentController* documentController,
    const std::vector<ara::SourceChoice>& choices,
    const juce::String& sourceId,
    const int takeNumber,
    const dsp::LinkwitzRileyCrossover::CrossoverFrequencies& frequencies,
    const size_t crossoverCount,
    const size_t columnCount,
    const bool includeScop,
    const bool includeSpec,
    const bool includeCorr,
    const bool includeLvls,
    const LvlsConfig& lvlsConfig,
    SpectrogramMapWriter* specMapWriter,
    const std::function<bool()>& shouldCancel,
    const std::function<void(float)>& onProgress)
{
    std::vector<const ara::SourceChoice*> selectedChoices;
    for (const auto& choice : choices)
    {
        if (! choice.hostEnumerated
            || (sourceId.isNotEmpty() && choice.sourceId != sourceId)
            || (takeNumber > 0 && choice.takeNumber != takeNumber))
            continue;

        selectedChoices.push_back(&choice);
    }

    if (selectedChoices.empty())
    {
        result.startTimeSeconds = 0.0;
        result.durationSeconds = 0.0;
        if (includeScop)
        {
            initialiseEnvelopes(result, columnCount);
            finishEnvelopes(result);
        }
        return true;
    }

    auto firstTime = std::numeric_limits<double>::max();
    auto lastTime = std::numeric_limits<double>::lowest();
    auto totalDuration = 0.0;
    for (const auto* choice : selectedChoices)
        totalDuration += std::max(0.0, choice->playbackDurationSeconds);
    auto completedDuration = 0.0;
    const auto firstPassScale = includeCorr ? 0.5f : 1.0f;
    for (const auto* choice : selectedChoices)
    {
        firstTime = std::min(firstTime, choice->playbackStartSeconds);
        lastTime = std::max(lastTime,
                            choice->playbackStartSeconds + choice->playbackDurationSeconds);
    }

    result.startTimeSeconds = firstTime;
    result.durationSeconds = std::max(0.0, lastTime - firstTime);
    if (includeScop)
        initialiseEnvelopes(result, columnCount);

    for (const auto* choice : selectedChoices)
    {
        if (shouldCancel())
            return false;

        const auto choiceDuration = std::max(0.0, choice->playbackDurationSeconds);
        auto* audioSource = findHostTakeAudioSource(documentController, *choice);
        if (audioSource == nullptr)
        {
            completedDuration += choiceDuration;
            onProgress(firstPassScale * static_cast<float>(
                completedDuration / std::max(0.001, totalDuration)));
            continue;
        }

        if (! analyseHostTakeChoice(result, *audioSource, *choice, frequencies,
                                    crossoverCount, includeScop, includeSpec,
                                    includeCorr, includeLvls, lvlsConfig, specMapWriter, shouldCancel,
                                    [&] (const float localProgress)
                                    {
                                        onProgress(firstPassScale * static_cast<float>(
                                            (completedDuration + choiceDuration * localProgress)
                                            / std::max(0.001, totalDuration)));
                                    }))
            return false;
        completedDuration += choiceDuration;
        onProgress(firstPassScale * static_cast<float>(
            completedDuration / std::max(0.001, totalDuration)));
    }

    if (includeCorr && result.corr != nullptr)
    {
        result.corr->beginAraMinimumPass();
        completedDuration = 0.0;
        for (const auto* choice : selectedChoices)
        {
            if (shouldCancel())
                return false;

            const auto choiceDuration = std::max(0.0, choice->playbackDurationSeconds);
            auto* audioSource = findHostTakeAudioSource(documentController, *choice);
            if (audioSource == nullptr)
            {
                completedDuration += choiceDuration;
                onProgress(0.5f + 0.5f * static_cast<float>(
                    completedDuration / std::max(0.001, totalDuration)));
                continue;
            }

            if (! analyseHostTakeChoice(result, *audioSource, *choice, frequencies,
                                        crossoverCount, false, false, true, false,
                                        lvlsConfig, nullptr, shouldCancel,
                                        [&] (const float localProgress)
                                        {
                                            onProgress(0.5f + 0.5f * static_cast<float>(
                                                (completedDuration + choiceDuration * localProgress)
                                                / std::max(0.001, totalDuration)));
                                        }))
                return false;
            completedDuration += choiceDuration;
            onProgress(0.5f + 0.5f * static_cast<float>(
                completedDuration / std::max(0.001, totalDuration)));
        }
    }

    if (includeScop)
        finishEnvelopes(result);
    return true;
}

std::shared_ptr<ara::AnalysisResult> analysePlaybackRegions(
    ARA::PlugIn::DocumentController* documentController,
    const std::vector<juce::ARAPlaybackRegion*>& playbackRegions,
    const ara::AnalysisRequest& request,
    const uint64_t analysisRevision,
    const std::function<bool()>& shouldCancel,
    const std::function<void(float)>& onProgress)
{
    const auto columnCount = std::max<size_t>(1, request.columnCount);
    const auto sourceChoices = request.sourceChoices.empty()
        ? makeSourceChoices(playbackRegions)
        : request.sourceChoices;

    auto result = std::make_shared<ara::AnalysisResult>();
    const auto includeScop = request.analyzerPage == AnalyzerPage::scop;
    const auto includeSpec = request.analyzerPage == AnalyzerPage::spec;
    const auto includeCorr = request.analyzerPage == AnalyzerPage::corr;
    const auto includeLvls = request.analyzerPage == AnalyzerPage::lvls;
    const auto crossoverCount = std::min(request.crossoverCount,
                                         dsp::LinkwitzRileyCrossover::numCrossovers);
    const LvlsConfig lvlsConfig {
        { request.includeLvlsPeakRms, request.includeLvlsLoudness, request.includeLvlsHistory },
        request.lvlsRmsWindowMs, request.lvlsPeakHoldMs
    };
    result->activeBandCount = crossoverCount + 1;
    result->revision = analysisRevision;
    std::unique_ptr<SpectrogramMapWriter> specMapWriter;
    if (includeSpec)
    {
        prepareSpec(*result, request.specFftSize, request.specFftOverlap,
                    request.specMapTimeOverlapFraction, request.specMapMode,
                    columnCount, request.specMapRowCount);
        if (request.specMapMode && result->specMap != nullptr)
            specMapWriter = std::make_unique<SpectrogramMapWriter>(
                *result->specMap, columnCount);
    }
    if (includeCorr)
        prepareCorr(*result, request.corrFftSize,
                                          request.corrFftOverlap,
                                          request.corrMode);
    if (includeLvls)
        prepareLvls(*result);

    if (std::any_of(sourceChoices.begin(), sourceChoices.end(),
                    [] (const auto& choice) { return choice.hostEnumerated; }))
    {
        if (! analyseHostTakeChoices(
                *result, documentController, sourceChoices,
                request.sourceId, request.takeNumber, request.frequencies,
                crossoverCount, columnCount, includeScop, includeSpec,
                includeCorr, includeLvls, lvlsConfig, specMapWriter.get(),
                shouldCancel, onProgress))
            return {};

        if (specMapWriter != nullptr)
            specMapWriter->finish();
        return result;
    }

    auto firstTime = std::numeric_limits<double>::max();
    auto lastTime = std::numeric_limits<double>::lowest();
    const auto shouldAnalyse = [&request, &sourceChoices] (
                                   const juce::ARAPlaybackRegion* playbackRegion)
    {
        return playbackRegion != nullptr
            && matchesSelection(*playbackRegion, sourceChoices,
                                       request.sourceId, request.takeNumber);
    };

    std::vector<juce::ARAPlaybackRegion*> selectedPlaybackRegions;
    selectedPlaybackRegions.reserve(playbackRegions.size());
    for (auto* playbackRegion : playbackRegions)
        if (shouldAnalyse(playbackRegion))
            selectedPlaybackRegions.push_back(playbackRegion);

    for (auto* playbackRegion : selectedPlaybackRegions)
    {
        firstTime = std::min(firstTime, playbackRegion->getStartInPlaybackTime());
        lastTime = std::max(lastTime, playbackRegion->getEndInPlaybackTime());
    }

    juce::ARAAudioModification* directTake = nullptr;
    if (firstTime > lastTime)
    {
        directTake = findTakeModification(
            documentController, sourceChoices, request.sourceId, request.takeNumber);
        if (directTake == nullptr || directTake->getAudioSource() == nullptr)
            return result;

        firstTime = 0.0;
        lastTime = directTake->getAudioSource()->getDuration();
    }

    result->startTimeSeconds = firstTime;
    result->durationSeconds = std::max(0.0, lastTime - firstTime);

    if (includeScop)
        initialiseEnvelopes(*result, columnCount);

    auto completedRegions = size_t { 0 };
    const auto regionCount = std::max<size_t>(1, selectedPlaybackRegions.size());
    const auto firstPassScale = includeCorr ? 0.5f : 1.0f;
    for (auto* playbackRegion : selectedPlaybackRegions)
    {
        if (shouldCancel())
            return {};

        auto* modification = playbackRegion != nullptr
            ? playbackRegion->getAudioModification() : nullptr;
        auto* audioSource = modification != nullptr ? modification->getAudioSource() : nullptr;
        if (audioSource == nullptr)
        {
            ++completedRegions;
            onProgress(firstPassScale * static_cast<float>(completedRegions)
                       / static_cast<float>(regionCount));
            continue;
        }

        const auto sourceRate = audioSource->getSampleRate();
        const auto sourceStart = std::max<juce::int64>(0, playbackRegion->getStartInAudioModificationSamples());
        const auto sourceEnd = std::min<juce::int64>(audioSource->getSampleCount(),
                                                     playbackRegion->getEndInAudioModificationSamples());

        if (sourceRate <= 0.0 || sourceEnd <= sourceStart)
        {
            ++completedRegions;
            onProgress(firstPassScale * static_cast<float>(completedRegions)
                       / static_cast<float>(regionCount));
            continue;
        }

        if (includeSpec && specMapWriter == nullptr && result->spec != nullptr)
            result->spec->resetAraStream();

        juce::ARAAudioSourceReader reader(audioSource);
        juce::AudioBuffer<float> readBuffer(2, readBlockSize);
        dsp::LinkwitzRileyCrossover crossover;
        crossover.prepare(sourceRate);
        crossover.setCrossoverCount(crossoverCount);
        crossover.setCrossoverFrequencies(request.frequencies);
        const auto playbackStart = playbackRegion->getStartInPlaybackTime();
        const auto playbackDuration = playbackRegion->getDurationInPlaybackTime();
        const auto sourceLength = static_cast<double>(sourceEnd - sourceStart);
        const auto sourceSamplesPerPlaybackSecond = playbackDuration > 0.0
            ? sourceLength / playbackDuration : 0.0;

        if (specMapWriter != nullptr && playbackDuration > 0.0)
        {
            if (! analyseMapSourceSegmentDirect(
                    *result, *audioSource, sourceStart, sourceEnd,
                    playbackStart, playbackStart + playbackDuration,
                    1.0, *specMapWriter,
                    [sourceStart, playbackStart, sourceSamplesPerPlaybackSecond] (const double sourceSample)
                    {
                        return playbackStart
                            + (sourceSample - static_cast<double>(sourceStart))
                                / sourceSamplesPerPlaybackSecond;
                    },
                    shouldCancel, [&] (const float localProgress)
                    {
                        onProgress(firstPassScale *
                            (static_cast<float>(completedRegions) + localProgress)
                            / static_cast<float>(regionCount));
                    }))
                return {};

            if (! includeScop && ! includeCorr && ! includeLvls)
            {
                ++completedRegions;
                onProgress(firstPassScale * static_cast<float>(completedRegions)
                           / static_cast<float>(regionCount));
                continue;
            }
        }

        for (auto readPosition = sourceStart; readPosition < sourceEnd; readPosition += readBlockSize)
        {
            if (shouldCancel())
                return {};

            const auto samplesToRead = static_cast<int>(std::min<juce::int64>(
                readBlockSize, sourceEnd - readPosition));

            if (! reader.read(&readBuffer, 0, samplesToRead, readPosition, true, true))
                continue;

            processBlock(*result, readBuffer, samplesToRead, sourceRate,
                     includeSpec && specMapWriter == nullptr, includeCorr, includeLvls, lvlsConfig);
            const auto localProgress = static_cast<float>(readPosition + samplesToRead - sourceStart)
                / static_cast<float>(sourceEnd - sourceStart);
            onProgress(firstPassScale * (static_cast<float>(completedRegions) + localProgress)
                       / static_cast<float>(regionCount));

            if (! includeScop)
                continue;

            const auto* left = readBuffer.getReadPointer(0);
            const auto* right = readBuffer.getReadPointer(1);

            for (int sampleIndex = 0; sampleIndex < samplesToRead; ++sampleIndex)
            {
                const auto sourceOffset = static_cast<double>(readPosition - sourceStart + sampleIndex);
                const auto playbackTime = playbackStart
                    + (sourceOffset / sourceLength) * playbackDuration;
                const auto normalizedTime = result->durationSeconds > 0.0
                    ? (playbackTime - firstTime) / result->durationSeconds
                    : 0.0;
                const auto column = std::min(columnCount - 1,
                    static_cast<size_t>(std::max(0.0, normalizedTime)
                                        * static_cast<double>(columnCount)));
                accumulateScopSample(*result, crossover, column, left[sampleIndex], right[sampleIndex]);
            }
        }

        ++completedRegions;
        onProgress(firstPassScale * static_cast<float>(completedRegions)
                   / static_cast<float>(regionCount));
    }

    if (directTake == nullptr && includeCorr && result->corr != nullptr)
    {
        result->corr->beginAraMinimumPass();

        completedRegions = 0;
        for (auto* playbackRegion : selectedPlaybackRegions)
        {
            if (shouldCancel())
                return {};

            auto* modification = playbackRegion != nullptr
                ? playbackRegion->getAudioModification() : nullptr;
            auto* audioSource = modification != nullptr ? modification->getAudioSource() : nullptr;
            if (audioSource == nullptr)
            {
                ++completedRegions;
                onProgress(0.5f + 0.5f * static_cast<float>(completedRegions)
                           / static_cast<float>(regionCount));
                continue;
            }

            const auto sourceRate = audioSource->getSampleRate();
            const auto sourceStart = std::max<juce::int64>(
                0, playbackRegion->getStartInAudioModificationSamples());
            const auto sourceEnd = std::min<juce::int64>(
                audioSource->getSampleCount(),
                playbackRegion->getEndInAudioModificationSamples());
            if (sourceRate <= 0.0 || sourceEnd <= sourceStart)
            {
                ++completedRegions;
                onProgress(0.5f + 0.5f * static_cast<float>(completedRegions)
                           / static_cast<float>(regionCount));
                continue;
            }

            juce::ARAAudioSourceReader reader(audioSource);
            juce::AudioBuffer<float> readBuffer(2, readBlockSize);
            for (auto readPosition = sourceStart;
                 readPosition < sourceEnd;
                 readPosition += readBlockSize)
            {
                if (shouldCancel())
                    return {};

                const auto samplesToRead = static_cast<int>(std::min<juce::int64>(
                    readBlockSize, sourceEnd - readPosition));
                if (! reader.read(&readBuffer, 0, samplesToRead, readPosition, true, true))
                    continue;

                processBlock(*result, readBuffer, samplesToRead, sourceRate,
                                            false, true, false, lvlsConfig);
                const auto localProgress = static_cast<float>(readPosition + samplesToRead - sourceStart)
                    / static_cast<float>(sourceEnd - sourceStart);
                onProgress(0.5f + 0.5f * (static_cast<float>(completedRegions) + localProgress)
                           / static_cast<float>(regionCount));
            }
            ++completedRegions;
            onProgress(0.5f + 0.5f * static_cast<float>(completedRegions)
                       / static_cast<float>(regionCount));
        }
    }

    if (directTake != nullptr)
    {
        if (! analyseTakeSource(
                *result, *directTake->getAudioSource(), request.frequencies,
                crossoverCount, includeScop, includeSpec,
                includeCorr, includeLvls, lvlsConfig, specMapWriter.get(),
                shouldCancel, [&] (const float progress)
                {
                    onProgress((includeCorr ? 0.5f : 1.0f) * progress);
                }))
            return {};

        if (includeCorr && result->corr != nullptr)
        {
            result->corr->beginAraMinimumPass();
            if (! analyseTakeSource(
                    *result, *directTake->getAudioSource(), request.frequencies,
                    crossoverCount, false, false, true, false,
                    lvlsConfig, nullptr, shouldCancel, [&] (const float progress)
                    {
                        onProgress(0.5f + 0.5f * progress);
                    }))
                return {};
        }
    }

    if (includeScop)
        finishEnvelopes(*result);
    if (specMapWriter != nullptr)
        specMapWriter->finish();

    return result;
}
}

#endif
