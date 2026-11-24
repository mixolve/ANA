#pragma once

#include <array>

namespace ana::settings_detail
{
inline constexpr std::array<const char*, 7> sectionKeys { "spec.freq", "spec.map", "corr.signed", "corr.phase", "corr.freq", "lvls", "scop" };
inline constexpr std::array<const char*, 7> sectionTitles { "FREQ", "MAP", "CORR SIGNED", "CORR PHASE", "CORR FREQ", "LVLS", "SCOP" };
}
