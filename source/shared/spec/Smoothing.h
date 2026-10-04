#pragma once

#include "FrequencyScale.h"
#include <vector>

namespace ana::spectrum_processing
{
// Values are linear power for SPEC and linear coefficients for CORR.
// Smoothing is calculated once in frequency coordinates, before any display
// scale, zoom or pixel aggregation is applied. DC is excluded from the curve.
class FrequencySmoothing
{
public:
    FrequencySmoothing(std::vector<double> fftValues, float smoothingPercent);
    double valueAtBinPosition(double position) const noexcept;
    std::vector<double> displayColumns(frequency_scale::Scale scale,
                                      float lowFrequency, float highFrequency,
                                      float binFrequency, int columnCount) const;

private:
    double averageLogInterval(double lower, double upper) const noexcept;
    std::vector<double> values;
    std::vector<double> logarithmicValues;
    double logarithmicOrigin = 0.0;
};
}
