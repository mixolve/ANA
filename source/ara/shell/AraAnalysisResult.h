#pragma once

#include "shared/scop/AnalysisTypes.h"
#include "shared/scop/Crossover.h"
#include "shared/spec/Channels.h"
#include "StereoFftStream.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace ana
{
namespace spec
{
class SpecProcessor;
}

namespace corr
{
class CorrProcessor;
}

namespace lvls
{
class LvlsProcessor;
}

namespace ara
{
struct SpectrogramMap
{
    static constexpr size_t channelCount = spec::channelCount;
    static constexpr size_t minimumRowCount = 2;
    static constexpr size_t maximumRowCount =
        static_cast<size_t>(fft::StereoFftStream::maximumFftSize / 2 + 1);

    // MAP rows are log-frequency mapped; Time Overlap oversamples the base time raster.
    size_t columnCount = 0;
    size_t rowCount = minimumRowCount;
    size_t binCount = 0;
    double sampleRate = 0.0;
    int fftSize = 0;
    std::vector<uint32_t> columnFrameCounts;
    // Raw STEREO bins keep LOG/range zoom render-only; other modes use mapped rows.
    std::vector<float> stereoDecibels; // column-major: column * binCount + bin
    std::array<std::vector<float>, channelCount> levels;

    bool isValid() const noexcept
    {
        if (columnCount == 0 || sampleRate <= 0.0)
            return false;

        if (rowCount < minimumRowCount || rowCount > maximumRowCount
            || ! fft::StereoFftStream::isSupportedFftSize(fftSize)
            || binCount != static_cast<size_t>(fftSize / 2 + 1)
            || columnFrameCounts.size() != columnCount
            || stereoDecibels.size() != columnCount * binCount)
            return false;

        const auto expectedSize = columnCount * rowCount;
        for (const auto& channel : levels)
            if (channel.size() != expectedSize)
                return false;
        return true;
    }
};

struct AnalysisResult
{
    std::array<scop::AnalysisChannelEnvelopes, dsp::LinkwitzRileyCrossover::numBands> bands;
    scop::AnalysisChannelEnvelopes wideband;
    size_t activeBandCount = 0;
    double startTimeSeconds = 0.0;
    double durationSeconds = 0.0;
    uint64_t revision = 0;
    std::shared_ptr<spec::SpecProcessor> spec;
    double specSampleRate = 0.0;
    int specFftSize = fft::StereoFftStream::defaultFftSize;
    float specFftOverlap = fft::StereoFftStream::defaultOverlap;
    float specMapTimeOverlapFraction = 0.0f;
    std::shared_ptr<SpectrogramMap> specMap;
    std::shared_ptr<corr::CorrProcessor> corr;
    double corrSampleRate = 0.0;
    int corrFftSize = fft::StereoFftStream::defaultFftSize;
    float corrFftOverlap = fft::StereoFftStream::defaultOverlap;
    int corrMode = 0;
    std::shared_ptr<lvls::LvlsProcessor> lvls;
    double lvlsSampleRate = 0.0;
};
}
}
