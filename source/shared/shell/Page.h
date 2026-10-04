#pragma once

#include <cstdint>

namespace ana
{
enum class AnalyzerPage : uint8_t
{
    spec,
    corr,
    lvls,
    scop
};

constexpr AnalyzerPage analyzerPageFromIndex(const int index) noexcept
{
    switch (index)
    {
        case 0: return AnalyzerPage::spec;
        case 1: return AnalyzerPage::corr;
        case 2: return AnalyzerPage::lvls;
        case 3: return AnalyzerPage::scop;
        default: return AnalyzerPage::spec;
    }
}
}
