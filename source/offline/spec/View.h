#pragma once

#include "shared/spec/Snapshots.h"

#include "shared/shell/Controls.h"
#include "shared/shell/ParameterControl.h"
#include "shared/spec/MonitorMode.h"

#include <cstddef>
#include <cstdint>
#include <JuceHeader.h>

#include <array>
#include <functional>
#include <memory>
#include <limits>
#include <vector>

class PluginProcessor;

class OfflineSpecView final : public juce::Component, private juce::Timer
{
public:
    explicit OfflineSpecView(PluginProcessor& processorRef,
                              std::vector<ana::spec::SpectrumSnapshot>& snapshotData);

    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;

    bool captureSnapshot(size_t snapshotIndex);

private:
    enum class ViewMode
    {
        frequency,
        map
    };

    void timerCallback() override;
    void syncRangeSliders();
    void updateFrequencyRangeFromSlider();
    void updateVerticalRangeFromSlider();
    void updateMapTimeRangeFromSlider();
    void updateMapTimeReadouts();
    void refreshControls();
    void setViewMode(ViewMode nextViewMode);
    void updateCursorReadouts();
    void rebuildOfflineSpectrogramImage();
    void scheduleMapImageRebuild() noexcept;
    juce::Rectangle<float> getPlotBounds() const noexcept;
    size_t getOfflineMapColumnCount() const noexcept;
    size_t getOfflineMapRowCount() const noexcept;

    PluginProcessor& processor;
    ParameterControl frequencyLowControl;
    ParameterControl frequencyHighControl;
    ParameterControl rangeLowControl;
    ParameterControl rangeHighControl;
    EllipsisLabel cursorReadoutLabel;
    EllipsisLabel cursorNoteReadoutLabel;
    EllipsisLabel cursorVerticalReadoutLabel;
    EllipsisLabel mapTimeStartReadoutLabel;
    EllipsisLabel mapTimeEndReadoutLabel;
    std::array<std::unique_ptr<ControlButton>, ana::spec::monitorModeCount> monitorButtons;
    ControlButton freqButton { "FREQ" };
    ControlButton mapButton { "MAP" };
    RangeSlider frequencyRangeSlider;
    RangeSlider mapTimeRangeSlider;
    RangeSlider magnitudeRangeSlider { RangeSlider::Orientation::vertical };
    std::vector<float> primarySpec;
    std::vector<float> secondarySpec;
    std::vector<ana::spec::SpectrumSnapshot>& snapshots;
    juce::Image offlineSpectrogramImage;
    ViewMode viewMode = ViewMode::frequency;
    uint64_t displayedClearRevision = 0;
    uint64_t displayedOfflineRevision = 0;
    int displayedFirstGraphType = -1;
    bool synchronisingRanges = false;
    juce::Point<float> cursorPosition;
    bool cursorInside = false;
    float lastCursorFrequency = 0.0f;
    float mapTimeRangeStart = 0.0f;
    float mapTimeRangeEnd = 1.0f;
    float renderedMapRangeLow = std::numeric_limits<float>::quiet_NaN();
    float renderedMapRangeHigh = std::numeric_limits<float>::quiet_NaN();
    float renderedMapSlope = std::numeric_limits<float>::quiet_NaN();
    int renderedMapHighQuality = -1;
    int renderedMapLeftToRight = -1;
    int renderedMapFrequencyScale = -1;
    int renderedMapColourMap = -1;
    bool mapImageRebuildPending = false;
    int renderedOfflineMonitorMode = -1;
    bool renderedOfflineSplitView = false;
};
