#pragma once

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

class SpecView final : public juce::Component, private juce::Timer
{
public:
    explicit SpecView(PluginProcessor& processorRef);

    static constexpr float snapshotGainMinimumDb = -48.0f;
    static constexpr float snapshotGainMaximumDb = 48.0f;

    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;

    size_t addSnapshotSlot();
    bool removeSnapshotSlot(size_t snapshotIndex);
    bool captureSnapshot(size_t snapshotIndex);
    void setSnapshotVisible(size_t snapshotIndex, bool shouldBeVisible);
    void setSnapshotColour(size_t snapshotIndex, juce::Colour colour);
    void setSnapshotGain(size_t snapshotIndex, float gainDb);
    bool writeSnapshot(size_t snapshotIndex, juce::OutputStream& output) const;
    bool readSnapshot(size_t snapshotIndex, juce::InputStream& input);
    bool hasSnapshotData(size_t snapshotIndex) const noexcept;
    bool isSnapshotVisible(size_t snapshotIndex) const noexcept;
    juce::Colour getSnapshotColour(size_t snapshotIndex) const noexcept;
    float getSnapshotGain(size_t snapshotIndex) const noexcept;
    juce::String getSettingsViewModeName() const;
    std::function<void(const juce::String&)> onAraUpdateStatus;

private:
    enum class ViewMode
    {
        frequency,
        map
    };

    struct SpectrumSnapshot
    {
        std::vector<float> primary;
        std::vector<float> secondary;
        int fftSize = 0;
        double sampleRate = 0.0;
        bool drawSecondGraph = false;
        juce::Colour colour { juce::Colours::white };
        float gainDb = 0.0f;
        bool visible = true;
        bool hasData = false;
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
    void rebuildAraSpectrogramImage();
    void scheduleMapImageRebuild() noexcept;
    juce::Rectangle<float> getPlotBounds() const noexcept;
    size_t getAraMapColumnCount() const noexcept;
    size_t getAraMapRowCount() const noexcept;
    void drawSpec(juce::Graphics& graphics, const std::vector<float>& spec,
                      int fftSize, double sampleRate, juce::Rectangle<float> plotBounds,
                      juce::Colour lineColour, juce::Colour fillColour,
                      bool allowFill = true, float gainDb = 0.0f) const;

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
    std::vector<SpectrumSnapshot> snapshots;
    juce::Image araSpectrogramImage;
    ViewMode viewMode = ViewMode::frequency;
    uint64_t displayedClearRevision = 0;
    uint64_t displayedAraRevision = 0;
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
    int renderedAraMonitorMode = -1;
    bool renderedAraSplitView = false;
};
