#pragma once

#include "shared/scop/Crossover.h"
#include "shared/shell/Theme.h"

namespace ana::scop
{
inline constexpr int bandRangeSliderHeight = 14;
inline constexpr int minimumRealtimeBandHeight = ana::ui::controlHeight
    + 2 * ana::ui::gap.pixels();
inline constexpr int minimumOfflineBandHeight = ana::ui::controlHeight
    + 3 * ana::ui::gap.pixels() + bandRangeSliderHeight;

constexpr int minimumEditorHeightForBands(const int minimumBandHeight) noexcept
{
    // The first and last bands supply their own edge gaps.
    return ana::ui::gap.pixels() + ana::ui::controlHeight
        + static_cast<int>(ana::dsp::LinkwitzRileyCrossover::numBands) * minimumBandHeight;
}
}
