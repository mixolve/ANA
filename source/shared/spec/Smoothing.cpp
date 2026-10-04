#include "Smoothing.h"
#include "SpectrumProcessing.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace ana::spectrum_processing
{
namespace
{
constexpr double pointsPerOctave = 384.0;
constexpr int gaussianPasses = 4;
constexpr double gaussianFwhm = 2.3548200450309493;
}

FrequencySmoothing::FrequencySmoothing(std::vector<double> fftValues,
                                      const float smoothingPercent)
    : values(std::move(fftValues))
{
    for (auto& value : values)
        if (! std::isfinite(value))
            value = 0.0;
    const auto bandwidth = smoothingBandwidthOctaves(smoothingPercent);
    if (values.size() < 2 || bandwidth <= 0.0)
        return;

    const auto sigma = bandwidth * pointsPerOctave / gaussianFwhm;
    const auto padding = static_cast<int>(std::ceil(6.0 * sigma));
    logarithmicOrigin = -1.0 - static_cast<double>(padding) / pointsPerOctave;
    const auto lastPosition = static_cast<double>(values.size() - 1);
    const auto gridCount = static_cast<size_t>(std::ceil(
        (std::log2(lastPosition) + 1.0) * pointsPerOctave))
        + 2 * static_cast<size_t>(padding) + 1;
    logarithmicValues.resize(gridCount);
    for (size_t index = 0; index < gridCount; ++index)
    {
        const auto centre = logarithmicOrigin + static_cast<double>(index) / pointsPerOctave;
        logarithmicValues[index] = averageLogInterval(
            std::exp2(centre - 0.5 / pointsPerOctave),
            std::exp2(centre + 0.5 / pointsPerOctave));
    }

    // Four forward/backward IIR pairs approximate a symmetric Gaussian.
    // Each pair has variance 2*a/(1-a)^2; choose a from the desired variance.
    const auto variancePerFilter = sigma * sigma / (2.0 * gaussianPasses);
    const auto update = 2.0 / (1.0 + std::sqrt(1.0 + 4.0 * variancePerFilter));
    for (int pass = 0; pass < gaussianPasses; ++pass)
    {
        auto previous = logarithmicValues.front();
        for (auto& value : logarithmicValues)
        {
            value = previous + update * (value - previous);
            previous = value;
        }
        previous = logarithmicValues.back();
        for (auto iterator = logarithmicValues.rbegin(); iterator != logarithmicValues.rend(); ++iterator)
        {
            *iterator = previous + update * (*iterator - previous);
            previous = *iterator;
        }
    }
}

double FrequencySmoothing::averageLogInterval(const double lower, const double upper) const noexcept
{
    const auto last = values.size() - 1;
    auto integral = 0.0;
    for (auto position = lower; position < upper;)
    {
        auto end = upper;
        auto firstValue = values[last];
        auto slope = 0.0;
        if (position < 1.0)
        {
            end = std::min(upper, 1.0);
            firstValue = values[1];
        }
        else if (position < static_cast<double>(last))
        {
            const auto bin = static_cast<size_t>(position);
            end = std::min(upper, static_cast<double>(bin + 1));
            slope = values[bin + 1] - values[bin];
            firstValue = values[bin] + slope * (position - static_cast<double>(bin));
        }
        const auto logWidth = std::log1p((end - position) / position);
        // Exact log-frequency integral of piecewise-linear FFT data. Integrating
        // all crossed segments preserves narrow peaks instead of missing them
        // when high-frequency grid cells cover several FFT bins.
        integral += firstValue * logWidth + slope * ((end - position) - position * logWidth);
        position = end;
    }
    return integral / std::log(upper / lower);
}

double FrequencySmoothing::valueAtBinPosition(const double position) const noexcept
{
    if (values.size() < 2 || ! std::isfinite(position))
        return 0.0;
    const auto last = values.size() - 1;
    const auto clamped = std::clamp(position, 1.0, static_cast<double>(last));
    if (logarithmicValues.empty())
    {
        const auto first = static_cast<size_t>(clamped);
        const auto second = std::min(last, first + 1);
        const auto mix = clamped - static_cast<double>(first);
        return values[first] + mix * (values[second] - values[first]);
    }
    const auto coordinate = std::clamp(
        (std::log2(clamped) - logarithmicOrigin) * pointsPerOctave,
        0.0, static_cast<double>(logarithmicValues.size() - 1));
    const auto first = static_cast<size_t>(coordinate);
    const auto second = std::min(logarithmicValues.size() - 1, first + 1);
    const auto mix = coordinate - static_cast<double>(first);
    return logarithmicValues[first] + mix * (logarithmicValues[second] - logarithmicValues[first]);
}

std::vector<double> FrequencySmoothing::displayColumns(const frequency_scale::Scale scale,
                                                       const float lowFrequency,
                                                       const float highFrequency,
                                                       const float binFrequency,
                                                       const int columnCount) const
{
    std::vector<double> columns(static_cast<size_t>(std::max(0, columnCount)),
                                std::numeric_limits<double>::quiet_NaN());
    if (values.size() < 2 || ! std::isfinite(binFrequency) || binFrequency <= 0.0f
        || ! std::isfinite(lowFrequency) || ! std::isfinite(highFrequency)
        || highFrequency <= lowFrequency || columnCount <= 0)
        return columns;
    if (! logarithmicValues.empty())
    {
        const auto nyquist = static_cast<double>(values.size() - 1) * binFrequency;
        for (int column = 0; column < columnCount; ++column)
        {
            const auto frequency = frequency_scale::frequencyAt(
                scale, lowFrequency, highFrequency,
                (static_cast<float>(column) + 0.5f) / static_cast<float>(columnCount));
            if (frequency <= nyquist)
                columns[static_cast<size_t>(column)] = valueAtBinPosition(frequency / binFrequency);
        }
        return columns;
    }

    // Zero means no smoothing: preserve the original per-pixel bin average.
    std::vector<int> counts(columns.size(), 0);
    for (size_t bin = 1; bin < values.size(); ++bin)
    {
        const auto frequency = static_cast<float>(bin) * binFrequency;
        if (frequency < lowFrequency || frequency > highFrequency)
            continue;
        const auto normalised = frequency_scale::normalisedForFrequency(
            scale, lowFrequency, highFrequency, frequency);
        const auto column = static_cast<size_t>(std::clamp(
            static_cast<int>(std::floor(normalised * static_cast<float>(columnCount))), 0, columnCount - 1));
        if (counts[column]++ == 0)
            columns[column] = values[bin];
        else
            columns[column] += values[bin];
    }
    for (size_t column = 0; column < columns.size(); ++column)
        if (counts[column] != 0)
            columns[column] /= counts[column];
    return columns;
}
}
