#pragma once

#include "Theme.h"
#include "shared/lvls/Settings.h"

namespace ana::ui
{
inline constexpr int minimumTrackNameWidth = 40;

inline int minimumMainEditorWidth() noexcept
{
    // The main window has no navigation or realtime/offline buttons.
    // Only the auxiliary actions, location and track labels take header width.
    constexpr int navWidth = 0;
    // All formerly symbolic header buttons now have textual preferred widths;
    // calculate the minimum using the same controls/gaps as PluginEditor::resized().
    // TAKE (TRACK) and KST (MONITOR) occupy the same position.
    const auto offlineActionsWidth = textControlWidth("inf")
        + textControlWidth("osc") + textControlWidth("sett")
        + textControlWidth("snapsh") + textControlWidth(2)
        + textControlWidth("refr")
        + juce::jmax(textControlWidth("TAKE"), textControlWidth("KST"))
        + 7 * gap.pixels();
    const auto locationWidth = juce::jmax(textControlWidth("TRACK"), textControlWidth("MONITOR"))
        + gap.pixels();
    const auto offlineHeaderWidth = 2 * gap.pixels() + navWidth
        + gap.pixels() + textControlWidth(3)
        + gap.pixels() + minimumTrackNameWidth
        + gap.pixels() + offlineActionsWidth
        + textControlWidth("OF") + gap.pixels() // SCOP full-source action
        + locationWidth + 1; // keep track name visible without a threshold match

    const auto realtimeActionsWidth = textControlWidth("inf")
        + textControlWidth("sett") + textControlWidth("snapsh")
        + textControlWidth("osc") + textControlWidth("FRZ")
        + textControlWidth("clear") + textControlWidth("OF")
        + 7 * gap.pixels();
    const auto realtimeHeaderWidth = 2 * gap.pixels() + navWidth
        + dragHandleSlotWidth + gap.pixels() + realtimeActionsWidth;

    // LVLS OTHERS must have enough space for *two* columns at the minimum
    // editor size, even after padding and the two inter-section gaps.
    const auto meterColumn = juce::jmax(ana::lvls::minimumMeterWidth + 2,
                                       textControlWidth(7));
    const auto peakWidth = 2 * meterColumn + gap.pixels();
    const auto loudnessWidth = 3 * meterColumn + 2 * gap.pixels();
    const auto othersCellWidth = textControlWidth("PLRD") + gap.pixels() + textControlWidth(7);
    const auto othersWidth = 2 * othersCellWidth + gap.pixels();
    const auto lvlsWidth = 2 * gap.pixels() + peakWidth + loudnessWidth
        + othersWidth + 2 * gap.pixels();

    return juce::jmax(juce::jmax(offlineHeaderWidth, realtimeHeaderWidth), lvlsWidth);
}
}
