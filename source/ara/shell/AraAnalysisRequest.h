#pragma once

#include "AraSourceChoice.h"
#include "shared/scop/Crossover.h"
#include "shared/lvls/Settings.h"
#include "shared/analyzer/Page.h"
#include "StereoFftStream.h"

#include <JuceHeader.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ana::ara
{
struct AnalysisRequest
{
    dsp::LinkwitzRileyCrossover::CrossoverFrequencies frequencies =
        dsp::LinkwitzRileyCrossover::defaultFrequencies;
    size_t crossoverCount = dsp::LinkwitzRileyCrossover::numCrossovers;
    size_t columnCount = 512;
    int specFftSize = fft::StereoFftStream::defaultFftSize;
    float specFftOverlap = fft::StereoFftStream::defaultOverlap;
    float specMapTimeOverlapFraction = 0.0f;
    bool specMapMode = false;
    size_t specMapRowCount = 2;
    int corrFftSize = fft::StereoFftStream::defaultFftSize;
    float corrFftOverlap = fft::StereoFftStream::defaultOverlap;
    int corrMode = 0;
    juce::String sourceId;
    int takeNumber = 0;
    std::vector<SourceChoice> sourceChoices;
    AnalyzerPage analyzerPage = AnalyzerPage::spec;
    bool includeLvlsPeakRms = true;
    bool includeLvlsLoudness = true;
    bool includeLvlsHistory = true;
    float lvlsRmsWindowMs = lvls::defaultRmsWindowMilliseconds;
    float lvlsPeakHoldMs = lvls::defaultPeakHoldMilliseconds;
    uint64_t regionGeneration = 0;
    uint64_t revision = 0;

    bool hasSameSettings(const AnalysisRequest& other) const
    {
        const auto usesHostEnumeratedCatalog = [] (const auto& choices)
        {
            return std::any_of(choices.begin(), choices.end(),
                               [] (const auto& choice) { return choice.hostEnumerated; });
        };
        const auto compareAraRegionGeneration =
            ! usesHostEnumeratedCatalog(sourceChoices)
            && ! usesHostEnumeratedCatalog(other.sourceChoices);

        if (analyzerPage != other.analyzerPage
            || sourceId != other.sourceId
            || takeNumber != other.takeNumber
            || sourceChoices != other.sourceChoices
            || (compareAraRegionGeneration && regionGeneration != other.regionGeneration))
            return false;

        switch (analyzerPage)
        {
            case AnalyzerPage::scop:
                return crossoverCount == other.crossoverCount
                    && frequencies == other.frequencies
                    && columnCount == other.columnCount;

            case AnalyzerPage::spec:
                // MAP cache identity excludes render-only time/frequency zoom.
                if (specMapMode != other.specMapMode || specFftSize != other.specFftSize)
                    return false;
                if (specMapMode)
                    // Raster geometry and Time Overlap are analysis state.
                    return columnCount == other.columnCount
                        && specMapRowCount == other.specMapRowCount
                        && juce::approximatelyEqual(specMapTimeOverlapFraction, other.specMapTimeOverlapFraction);
                return juce::approximatelyEqual(specFftOverlap, other.specFftOverlap);

            case AnalyzerPage::corr:
                return corrFftSize == other.corrFftSize
                    && juce::approximatelyEqual(corrFftOverlap, other.corrFftOverlap)
                    && corrMode == other.corrMode;

            case AnalyzerPage::lvls:
                return includeLvlsPeakRms == other.includeLvlsPeakRms
                    && includeLvlsLoudness == other.includeLvlsLoudness
                    && includeLvlsHistory == other.includeLvlsHistory
                    && juce::approximatelyEqual(lvlsRmsWindowMs, other.lvlsRmsWindowMs)
                    && juce::approximatelyEqual(lvlsPeakHoldMs, other.lvlsPeakHoldMs);
        }

        return false;
    }
};
}
