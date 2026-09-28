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
    constexpr int araRightControlCount = 7;
    const auto araRightControlsWidth = 3 * iconControlSize
        + iconControlSize + textControlWidth("TAKE")
        + textControlWidth("SOURCE") + textControlWidth(11)
        + araRightControlCount * gap.pixels();
    const auto regularWidth = 2 * gap.pixels()
        + mainNavigationWidth() + araRightControlsWidth;
    return regularWidth + iconControlSize + gap.pixels();
}
} // namespace ana::ui
