#include "Analyzer.h"
#include "SourceCatalog.h"

#include "shared/corr/Processor.h"
#include "shared/scop/AnalysisTypes.h"
#include "shared/spec/FrequencyScale.h"
#include "shared/spec/SpectrumChannelLevels.h"
#include "shared/spec/SpectrogramFrequencyScale.h"
#include "shared/spec/Processor.h"
#include "offline/spec/MapFrameStream.h"
#include "offline/spec/MapWriter.h"
#include "shared/lvls/Processor.h"


#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

namespace ana::offline
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

void accumulateScopSample(offline::AnalysisResult& result,
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

}

static void prepareSpec(offline::AnalysisResult& result,
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
        result.specMap = std::make_shared<offline::SpectrogramMap>();
        result.specMap->fftSize = fftSize;
        result.specMap->columnCount = std::max<size_t>(1, mapColumnCount);
        result.specMap->rowCount = std::clamp<size_t>(
            mapRowCount,
            offline::SpectrogramMap::minimumRowCount,
            offline::SpectrogramMap::maximumRowCount);
    }
}

static void processSpecBlock(offline::AnalysisResult& result,
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

    result.spec->processOfflineBlock(buffer, result.specFftSize, result.specFftOverlap);
}

static void prepareCorr(offline::AnalysisResult& result,
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

static void processCorrBlock(offline::AnalysisResult& result,
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
    result.corr->processOfflineBlock(buffer, result.corrFftSize,
                                              result.corrFftOverlap,
                                              corrMode);
}

static void prepareLvls(offline::AnalysisResult& result)
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

static void processLvlsBlock(offline::AnalysisResult& result,
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
    offline::AnalysisResult& result,
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
    offline::AnalysisResult& result,
    juce::AudioFormatReader& reader,
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

    const auto sourceRate = reader.sampleRate;
    const auto sourceSampleCount = reader.lengthInSamples;
    if (sourceRate <= 0.0 || sourceSampleCount <= 0 || validSourceEnd <= validSourceStart
        || playbackEnd <= playbackStart)
        return true;

    // FFTs follow each source's native rate; the writer maps them onto one result grid.
    if (result.specSampleRate <= 0.0)
        result.specSampleRate = sourceRate;
    result.spec->prepare(sourceRate);
    result.spec->resetOfflineStream();
    const auto segmentStart = playbackTimeForSourceSample(static_cast<double>(validSourceStart));
    const auto segmentEnd = playbackTimeForSourceSample(static_cast<double>(validSourceEnd));
    writer.addTimeSpan((segmentStart - result.startTimeSeconds) / result.durationSeconds,
                      (segmentEnd - result.startTimeSeconds) / result.durationSeconds);
    const auto sourceSamplesPerPlaybackSecond = static_cast<double>(validSourceEnd - validSourceStart)
        / (segmentEnd - segmentStart);
    const auto mapHopSize = mapHopSizeForRaster(
        sourceSamplesPerPlaybackSecond, result.durationSeconds,
        writer.getColumnCount(), result.specMapTimeOverlapFraction);
    return readSpectrogramFrames(reader, validSourceStart, validSourceEnd,
        result.specFftSize, mapHopSize, static_cast<float>(gain),
        [&result] (const auto& buffer, const int fftSize, const int hop, const auto& onFrame)
        {
            result.spec->processOfflineMapBlock(buffer, fftSize, hop, onFrame);
        },
        [&] (const fft::StereoFftFrame& frame, const double centreSourceSample)
        {
            const auto time = playbackTimeForSourceSample(centreSourceSample);
            writer.addFrame(frame, sourceRate,
                            (time - result.startTimeSeconds) / result.durationSeconds);
        }, shouldCancel, onProgress);
}

static bool analyseTakeSource(
    offline::AnalysisResult& result,
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
        juce::ARAAudioSourceReader mapReader(&source);
        if (! analyseMapSourceSegmentDirect(
                result, mapReader, 0, sourceSampleCount,
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
        result.spec->resetOfflineStream();

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

static void initialiseEnvelopes(offline::AnalysisResult& result, const size_t columnCount)
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

static void finishEnvelopes(offline::AnalysisResult& result)
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
    offline::AnalysisResult& result,
    juce::AudioFormatReader& reader,
    const offline::SourceChoice& choice,
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
    const auto sourceRate = reader.sampleRate;
    const auto sourceSampleCount = reader.lengthInSamples;
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
                result, reader, sourceStart, sourceEnd,
                choice.playbackStartSeconds,
                choice.playbackStartSeconds + choice.playbackDurationSeconds,
                // Analyze source audio only; applying REAPER D_VOL here would add an unmatched timeline gain.
                1.0, *specMapWriter,
                [&choice, requestedSourceStart, sourceSamplesPerPlaybackSecond] (const double sourceSample)
                {
                    return choice.playbackStartSeconds
                        + (sourceSample - static_cast<double>(requestedSourceStart))
                            / sourceSamplesPerPlaybackSecond;
                },
                shouldCancel, onProgress))
            return false;

        if (! includeScop && ! includeCorr && ! includeLvls)
            return true;
    }

    if (includeSpec && specMapWriter == nullptr && result.spec != nullptr)
        result.spec->resetOfflineStream();

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
    offline::AnalysisResult& result,
    ARA::PlugIn::DocumentController* documentController,
    const std::vector<offline::SourceChoice>& choices,
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
    std::vector<const offline::SourceChoice*> selectedChoices;
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
        result.selectionEmpty = true;
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
        auto reader = createHostTakeReader(documentController, *choice);
        if (reader == nullptr)
        {
            completedDuration += choiceDuration;
            onProgress(firstPassScale * static_cast<float>(
                completedDuration / std::max(0.001, totalDuration)));
            continue;
        }

        if (! analyseHostTakeChoice(result, *reader, *choice, frequencies,
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
        result.corr->beginOfflineMinimumPass();
        completedDuration = 0.0;
        for (const auto* choice : selectedChoices)
        {
            if (shouldCancel())
                return false;

            const auto choiceDuration = std::max(0.0, choice->playbackDurationSeconds);
            auto reader = createHostTakeReader(documentController, *choice);
            if (reader == nullptr)
            {
                completedDuration += choiceDuration;
                onProgress(0.5f + 0.5f * static_cast<float>(
                    completedDuration / std::max(0.001, totalDuration)));
                continue;
            }

            if (! analyseHostTakeChoice(result, *reader, *choice, frequencies,
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

std::shared_ptr<offline::AnalysisResult> analysePlaybackRegions(
    ARA::PlugIn::DocumentController* documentController,
    const std::vector<juce::ARAPlaybackRegion*>& playbackRegions,
    const offline::AnalysisRequest& request,
    const uint64_t analysisRevision,
    const std::function<bool()>& shouldCancel,
    const std::function<void(float)>& onProgress)
{
    const auto columnCount = std::max<size_t>(1, request.columnCount);
    const auto sourceChoices = ! request.useHostTakeChoices && request.sourceChoices.empty()
        ? makeSourceChoices(playbackRegions)
        : request.sourceChoices;

    auto result = std::make_shared<offline::AnalysisResult>();
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

    if (request.useHostTakeChoices || std::any_of(sourceChoices.begin(), sourceChoices.end(),
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
        {
            result->selectionEmpty = true;
            return result;
        }

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
            result->spec->resetOfflineStream();

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
            juce::ARAAudioSourceReader mapReader(audioSource);
            if (! analyseMapSourceSegmentDirect(
                    *result, mapReader, sourceStart, sourceEnd,
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
        result->corr->beginOfflineMinimumPass();

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
            result->corr->beginOfflineMinimumPass();
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
