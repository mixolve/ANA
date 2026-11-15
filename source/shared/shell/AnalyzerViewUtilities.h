#pragma once

#include "shared/shell/Controls.h"
#include "Processor.h"
#include "shared/shell/Theme.h"
#include "shared/scop/Layout.h"
#include "shared/spec/FrequencyScale.h"

#include <cstdint>
#include <initializer_list>
#include <algorithm>
#include <cmath>

namespace ana::ui::analyzer_detail
{
// Slider sizing follows the SCOP band layout so view zoom lanes stay aligned.
inline constexpr int bandRangeSliderHeight = ana::scop::bandRangeSliderHeight;
inline constexpr int bandZoomSliderWidth = bandRangeSliderHeight;
inline int topRightOverlayWidth(const juce::Component& component)
{
    for(auto* current=&component;current!=nullptr;current=current->getParentComponent())
        if(current->getProperties().contains("topRightOverlayWidth")) return int(current->getProperties()["topRightOverlayWidth"]);
    return 0;
}

inline int topLeftOverlayWidth(const juce::Component& component)
{
    for(auto* current=&component;current!=nullptr;current=current->getParentComponent())
        if(current->getProperties().contains("topLeftOverlayWidth")) return int(current->getProperties()["topLeftOverlayWidth"]);
    return 0;
}

inline int topLeftOverlayBottom(const juce::Component& component)
{
    for(auto* current=&component;current!=nullptr;current=current->getParentComponent())
        if(current->getProperties().contains("topLeftOverlayBottom")) return int(current->getProperties()["topLeftOverlayBottom"]);
    return controlHeight;
}

// Position cursor labels in the window header rather than along the bottom axis.
// The first row excludes the UPDATE/TAKE overlays and the upper axis readout.
// At narrow sizes, labels wrap into the next row at the top of the plot.
struct CursorTopReadout
{
    juce::Component* component = nullptr;
    int width = 0; // zero means this readout is disabled
};

inline void layoutCursorReadoutsAtTop(const juce::Rectangle<int> availableTopRow,
                                      const juce::Rectangle<int> plotBounds,
                                      std::initializer_list<CursorTopReadout> readouts)
{
    const auto gapWidth = ana::ui::gap.pixels();
    int totalWidth = 0;
    int count = 0;
    for (const auto& readout : readouts)
    {
        if (readout.component == nullptr)
            continue;
        readout.component->setBounds({});
        if (readout.width <= 0)
            continue;
        totalWidth += readout.width;
        if (count++ > 0)
            totalWidth += gapWidth;
    }
    if (count == 0)
        return;

    const auto fitsHeader = totalWidth <= availableTopRow.getWidth();
    auto x = fitsHeader
        ? availableTopRow.getX() + (availableTopRow.getWidth() - totalWidth) / 2
        : plotBounds.getX();
    auto y = fitsHeader
        ? availableTopRow.getY()
        : availableTopRow.getBottom() + gapWidth;
    const auto right = fitsHeader ? availableTopRow.getRight() : plotBounds.getRight();
    const auto rowLeft = fitsHeader ? availableTopRow.getX() : plotBounds.getX();

    for (const auto& readout : readouts)
    {
        if (readout.component == nullptr || readout.width <= 0)
            continue;
        if (! fitsHeader && x > rowLeft && x + readout.width > right)
        {
            x = rowLeft;
            y += ana::ui::controlHeight + gapWidth;
        }
        readout.component->setBounds(x, y, readout.width, ana::ui::controlHeight);
        x += readout.width + gapWidth;
    }
}

inline void clearCursorReadouts(juce::Label& horizontal, juce::Label& notes, juce::Label& vertical)
{
    for (auto* label : { &horizontal, &notes, &vertical })
        label->setText({}, juce::dontSendNotification);
}

enum class CursorHoverUpdate { none, readouts, readoutsAndRepaint };

// Shared SPEC cursor hover state. Returns what the caller must refresh:
// readouts only, readouts plus repaint, or nothing.
inline CursorHoverUpdate trackCursorHover(bool& inside, juce::Point<float>& cursor,
                                          const juce::Rectangle<float> plotBounds,
                                          const juce::Point<float> position,
                                          const bool cleanView) noexcept
{
    if (cleanView)
    {
        inside = false;
        return CursorHoverUpdate::readouts;
    }

    if (! plotBounds.contains(position))
    {
        if (! inside)
            return CursorHoverUpdate::none;
        inside = false;
        return CursorHoverUpdate::readoutsAndRepaint;
    }

    if (inside && cursor == position)
        return CursorHoverUpdate::none;

    inside = true;
    cursor = position;
    return CursorHoverUpdate::readoutsAndRepaint;
}

template <typename UpdateReadouts>
inline void handleCursorHover(juce::Component& view, bool& inside,
                              juce::Point<float>& cursor,
                              const juce::Rectangle<float> plotBounds,
                              const juce::Point<float> position,
                              const bool cleanView, UpdateReadouts&& updateReadouts)
{
    switch (trackCursorHover(inside, cursor, plotBounds, position, cleanView))
    {
        case CursorHoverUpdate::readouts:
            updateReadouts();
            break;
        case CursorHoverUpdate::readoutsAndRepaint:
            updateReadouts();
            view.repaint();
            break;
        case CursorHoverUpdate::none:
            break;
    }
}

inline juce::Rectangle<float> specPlotBounds(const juce::Component& view,
                                             PluginProcessor& processor,
                                             const bool frequencyMode,
                                             const bool mapMode,
                                             const bool horizontalZoomInMap) noexcept
{
    auto bounds = view.getLocalBounds().toFloat();
    if (processor.isCleanView())
        return bounds;

    const auto* horizontalZoom = processor.getRawParameterValue(
        PluginProcessor::specHorizontalZoomParameterId);
    const auto* verticalZoom = processor.getRawParameterValue(
        PluginProcessor::specVerticalZoomParameterId);
    const auto showHorizontalZoom = (frequencyMode || (mapMode && horizontalZoomInMap))
        && (horizontalZoom == nullptr
            || horizontalZoom->load(std::memory_order_relaxed) >= 0.5f);
    const auto showVerticalZoom = (frequencyMode || mapMode)
        && (verticalZoom == nullptr
            || verticalZoom->load(std::memory_order_relaxed) >= 0.5f);
    if (showHorizontalZoom)
        bounds.removeFromBottom(
            static_cast<float>(bandRangeSliderHeight + ana::ui::gap.pixels()));
    if (showVerticalZoom)
        bounds.removeFromRight(
            static_cast<float>(bandZoomSliderWidth + ana::ui::gap.pixels()));
    return bounds;
}

inline bool isInsidePluginWindow(juce::Component& view, const juce::Point<int> screenPosition)
{
    auto* editor = view.findParentComponentOfClass<juce::AudioProcessorEditor>();
    const auto bounds = editor != nullptr ? editor->getScreenBounds() : view.getScreenBounds();
    return bounds.contains(screenPosition);
}

inline void handleCursorExit(juce::Component& view, const juce::Point<int> screenPosition,
                             bool& inside, juce::Label& horizontal,
                             juce::Label& notes, juce::Label& vertical)
{
    inside = false;
    if (! isInsidePluginWindow(view, screenPosition))
        clearCursorReadouts(horizontal, notes, vertical);
}

inline juce::String formatReadoutFrequency(const double frequency)
{
    return juce::String::formatted("%08.2f", std::max(0.0, frequency));
}

inline juce::String formatReadoutLevel(const double level)
{
    return juce::String::formatted("%+07.2f", level);
}

inline juce::String formatCorrCoefficient(const double coefficient)
{
    return juce::String::formatted("%+.2f", coefficient);
}

inline float readParameterValue(const PluginProcessor& processor,
                                const char* parameterId,
                                const float fallback) noexcept
{
    if (const auto* value = processor.getRawParameterValue(parameterId))
        return value->load(std::memory_order_relaxed);

    return fallback;
}

inline void setParameterPlainValue(PluginProcessor& processor, const char* parameterId,
                                   const float value)
{
    auto* parameter = processor.getActiveParameter(parameterId);
    if (parameter == nullptr || ! std::isfinite(value))
        return;
    const auto normalised = parameter->convertTo0to1(value);
    if (juce::approximatelyEqual(parameter->getValue(), normalised))
        return;
    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost(normalised);
    parameter->endChangeGesture();
}

inline bool updateOfflineRevision(const PluginProcessor& processor,
                                  uint64_t& displayedRevision)
{
    const auto analysisResult = processor.getAnalysisResult();
    if (analysisResult == nullptr || displayedRevision == analysisResult->revision)
        return false;

    displayedRevision = analysisResult->revision;
    return true;
}

inline void configureCursorReadoutLabel(EllipsisLabel& label,
                                        juce::Colour background = ana::ui::dark)
{
    label.setFont(ana::ui::makeFont());
    label.setJustificationType(juce::Justification::centred);
    label.setHideWhenEmpty(true);
    label.setText("---", juce::dontSendNotification);
    label.setColour(juce::Label::textColourId, ana::ui::white);
    label.setColour(juce::Label::backgroundColourId, background);
    label.setColour(juce::Label::outlineColourId, ana::ui::light);
    label.setBorderSize(juce::BorderSize<int>(1, ana::ui::gap.pixels(), 1, ana::ui::gap.pixels()));
    label.setTextVerticalOffset(ana::ui::readoutTextVerticalOffset);
    label.setInterceptsMouseClicks(false, false);
}
}
