#pragma once

#include "Theme.h"

namespace ana::ui
{
inline int mainNavigationWidth() noexcept
{
    return textControlWidth("SPEC") + textControlWidth("CORR")
        + textControlWidth("LVLS") + textControlWidth("SCOP")
        + 3 * gap.pixels();
}

inline int minimumMainEditorWidth() noexcept
{
    constexpr int offlineRightControlCount = 8;
    const auto offlineRightControlsWidth = 3 * iconControlSize
        + iconControlSize + textControlWidth("TAKE")
        + textControlWidth("KST") + textControlWidth("SOURCE") + textControlWidth(2)
        + offlineRightControlCount * gap.pixels();
    const auto regularWidth = 2 * gap.pixels()
        + mainNavigationWidth() + offlineRightControlsWidth
        + textControlWidth("MODE") + gap.pixels() + iconControlSize + gap.pixels();
    return regularWidth;
}
} // namespace ana::ui
