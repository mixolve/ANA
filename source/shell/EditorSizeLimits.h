#pragma once

#include "shared/scop/Layout.h"
#include "shared/shell/EditorMinimumWidth.h"

namespace ana::editor_detail
{
inline constexpr int minimumEditorHeight = ana::scop::minimumEditorHeightForBands(
    ana::scop::minimumRealtimeBandHeight);
inline constexpr int maximumEditorSize = 32768;
inline constexpr int editorResizeHandleThickness = ana::ui::resizeHandleThickness;

inline int minimumEditorWidth() noexcept
{
    return ana::ui::minimumMainEditorWidth();
}
}
