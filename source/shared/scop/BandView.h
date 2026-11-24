#pragma once

#include "shared/scop/ChannelData.h"
#include "shared/scop/Display.h"
#include "shared/scop/Layout.h"
#include "shared/shell/PlotVisibility.h"
#include "shared/shell/Controls.h"
#include "shared/shell/AnalyzerViewUtilities.h"
#include "shared/shell/Theme.h"
#include "shell/Processor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>

namespace ana::scop
{
class BandView : public juce::Component
{
public:
    explicit BandView(PluginProcessor& processorRef)
        : processor(processorRef)
    {
        setOpaque(false);
        bandHeightWeights = processor.getScopBandHeightWeights();
        singleViewBand = processor.getScopSingleViewBand();
        fullSourceView = processor.isScopFullSourceView();
    }

    void setLinkedNormalization(bool enabled)
    {
        if (processor.isScopLinkedNormalized() != enabled)
            applyLinkedNormalization(enabled);
        else if (enabled)
            refreshActiveBandNormalizations();
        refreshBandModeButtons();
        repaint();
    }

    void refreshBandLayout()
    {
        bandHeightWeights=processor.getScopBandHeightWeights();
        draggedBandSeparator=-1;
        setMouseCursor(juce::MouseCursor::NormalCursor);
        refreshBandModeButtons();
        repaint();
    }

    void setFullSourceView(const bool shouldShowFullSource)
    {
        if (fullSourceView == shouldShowFullSource)
            return;

        fullSourceView = shouldShowFullSource;
        if (fullSourceView)
        {
            singleViewBand = -1;
            processor.setScopSingleViewBand(-1);
        }

        draggedBandSeparator = -1;
        refreshBandModeButtons();
        repaint();
    }

    void mouseMove(const juce::MouseEvent& event) override
    {
        const auto activeBandCount = processor.getCrossoverCount() + 1;
        setMouseCursor(findBandSeparator(event.y, activeBandCount) >= 0
            ? juce::MouseCursor::UpDownResizeCursor
            : juce::MouseCursor::NormalCursor);
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        if (draggedBandSeparator < 0)
            setMouseCursor(juce::MouseCursor::NormalCursor);
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        draggedBandSeparator = findBandSeparator(
            event.y, processor.getCrossoverCount() + 1);

        if (draggedBandSeparator >= 0)
            setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (processor.areScopBandsEqualHeight()) { refreshBandLayout(); return; }
        const auto activeBandCount = processor.getCrossoverCount() + 1;
        const ana::scop::BandLayout layout {
            getWidth(), getHeight(), minimumBandHeight(), bandHeightWeights,
            fullSourceView ? 0 : singleViewBand
        };
        if (! layout.resizeSeparator(
                bandHeightWeights, draggedBandSeparator, event.y, activeBandCount))
            return;

        refreshBandModeButtons();
        repaint();
    }

    void mouseUp(const juce::MouseEvent& event) override
    {
        const auto wasDragging = draggedBandSeparator >= 0;
        draggedBandSeparator = -1;
        if (wasDragging)
            processor.setScopBandHeightWeights(bandHeightWeights);
        mouseMove(event);
    }

protected:
    virtual int minimumBandHeight() const noexcept = 0;
    virtual void clearBandHistory(size_t bandIndex) = 0;
    virtual const ana::scop::Envelope* getBandEnvelope(
        size_t bandIndex, size_t channelIndex) const noexcept = 0;

    virtual bool isBandNormalizationEnabled(size_t bandIndex) const
    {
        juce::ignoreUnused(bandIndex);
        return true;
    }

    virtual void createBandExtras(size_t bandIndex)
    {
        juce::ignoreUnused(bandIndex);
    }

    virtual void refreshBandExtras(
        size_t bandIndex, juce::Rectangle<int> laneBounds, bool isVisibleBand,
        bool zoomAvailable, bool showVerticalZoomSlider, int topControlsBottom)
    {
        juce::ignoreUnused(bandIndex, laneBounds, isVisibleBand, zoomAvailable,
                           showVerticalZoomSlider, topControlsBottom);
    }

    ana::ScopChannelMode getDisplayedChannelMode(const size_t bandIndex) const noexcept
    {
        return fullSourceView ? processor.getScopOverallMonitorMode()
                              : processor.getScopChannelMode(bandIndex);
    }

    void createBandControls()
    {
        for (size_t bandIndex = 0; bandIndex < bandModeButtons.size(); ++bandIndex)
        {
            for (size_t modeIndex = 0; modeIndex < bandModeButtons[bandIndex].size(); ++modeIndex)
            {
                auto button = std::make_unique<ControlButton>(display::scopModeButtonNames[modeIndex]);
                button->onClick = [this, bandIndex, modeIndex]
                {
                    const auto mode = display::scopModeButtonModes[modeIndex];

                    if (processor.getScopChannelMode(bandIndex) != mode)
                    {
                        processor.setScopChannelMode(bandIndex, mode);
                        processor.setScopBandNormalized(bandIndex, false);
                    }

                    refreshBandModeButtons();
                };
                addAndMakeVisible(*button);
                bandModeButtons[bandIndex][modeIndex] = std::move(button);
            }

            auto clearButton = std::make_unique<ControlButton>("clear");
            clearButton->setTooltip("CLEAR");
            clearButton->onClick = [this, bandIndex] { clearBandHistory(bandIndex); };
            addAndMakeVisible(*clearButton);
            bandClearButtons[bandIndex] = std::move(clearButton);

            auto singleViewButton = std::make_unique<ControlButton>("OF");
            singleViewButton->setTooltip("FULL");
            singleViewButton->onClick = [this, bandIndex]
            {
                if (fullSourceView)
                {
                    fullSourceView = false;
                    processor.setScopFullSourceView(false);
                }
                singleViewBand = singleViewBand == static_cast<int>(bandIndex)
                    ? -1
                    : static_cast<int>(bandIndex);
                processor.setScopSingleViewBand(singleViewBand);
                draggedBandSeparator = -1;
                refreshBandModeButtons();
                repaint();
            };
            addAndMakeVisible(*singleViewButton);
            bandSingleViewButtons[bandIndex] = std::move(singleViewButton);

            auto zoomSlider = std::make_unique<ClickArmedSlider>();
            zoomSlider->setLookAndFeel(&bandZoomLookAndFeel);
            zoomSlider->setSliderStyle(juce::Slider::LinearBarVertical);
            zoomSlider->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
            zoomSlider->setRange(ana::scop::minimumVerticalZoomDecibels,
                                 ana::scop::maximumVerticalZoomDecibels,
                                 ana::scop::verticalZoomStepDecibels);
            zoomSlider->setValue(processor.getScopVerticalZoomDecibels(bandIndex),
                                 juce::dontSendNotification);
            zoomSlider->setSliderSnapsToMousePosition(false);
            zoomSlider->setScrollWheelEnabled(true);
            zoomSlider->setWantsKeyboardFocus(false);
            zoomSlider->setDoubleClickReturnValue(true, 0.0);
            zoomSlider->onValueChange = [this, bandIndex, slider = zoomSlider.get()]
            {
                if (! updatingBandControls)
                {
                    processor.setScopBandNormalized(bandIndex, false);
                    if (processor.isScopLinkedNormalized())
                        processor.setScopLinkedNormalized(false);
                    processor.setScopVerticalZoomDecibels(
                        bandIndex, static_cast<float>(slider->getValue()));
                    refreshBandModeButtons();
                    repaint();
                }
            };
            addAndMakeVisible(*zoomSlider);
            bandZoomSliders[bandIndex] = std::move(zoomSlider);

            auto zoomValueLabel = std::make_unique<EllipsisLabel>();
            zoomValueLabel->setFont(ana::ui::makeFont());
            zoomValueLabel->setJustificationType(juce::Justification::centred);
            zoomValueLabel->setColour(juce::Label::textColourId, ana::ui::white);
            zoomValueLabel->setColour(juce::Label::textWhenEditingColourId, ana::ui::white);
            zoomValueLabel->setColour(juce::Label::backgroundColourId, ana::ui::dark);
            zoomValueLabel->setColour(juce::Label::outlineColourId, ana::ui::light);
            zoomValueLabel->setBorderSize(juce::BorderSize<int>(1));
            zoomValueLabel->setTextVerticalOffset(ana::ui::readoutTextVerticalOffset);
            zoomValueLabel->setEditable(true, false, false);
            zoomValueLabel->onTextChange = [this, bandIndex, label = zoomValueLabel.get()]
            {
                const auto text = label->getText().trim();
                if (text.isNotEmpty() && text.containsOnly("0123456789.+-")
                    && text.containsAnyOf("0123456789"))
                    bandZoomSliders[bandIndex]->setValue(
                        text.getDoubleValue(), juce::sendNotificationSync);
                if (! label->isBeingEdited())
                    label->setText(
                        display::formatZoomValue(
                            processor.getScopVerticalZoomDecibels(bandIndex)),
                        juce::dontSendNotification);
            };
            addAndMakeVisible(*zoomValueLabel);
            bandZoomValueLabels[bandIndex] = std::move(zoomValueLabel);

            createBandExtras(bandIndex);

            auto normalizeButton = std::make_unique<ControlButton>("N");
            normalizeButton->setTooltip("NORMALIZE");
            normalizeButton->onClick = [this, bandIndex] { normalizeBandWithZoom(bandIndex); };
            addAndMakeVisible(*normalizeButton);
            bandNormalizeButtons[bandIndex] = std::move(normalizeButton);
        }

    }

    void refreshBandModeButtons()
    {
        const auto activeBandCount = processor.getCrossoverCount() + 1;
        const auto persistedSingleViewBand = fullSourceView
            ? -1 : processor.getScopSingleViewBand();

        if (persistedSingleViewBand != singleViewBand)
            singleViewBand = persistedSingleViewBand;

        if (singleViewBand >= static_cast<int>(activeBandCount))
        {
            singleViewBand = -1;
            processor.setScopSingleViewBand(-1);
        }

        constexpr int buttonHeight = ana::ui::controlHeight;
        const auto zoomValueWidth = ana::scop::bandZoomValueWidth();
        const auto verticalZoomAvailable = processor.areScopVerticalZoomControlsVisible();
        const auto showVerticalReadout = processor.areScopVerticalReadoutsVisible();
        const auto showMonitorControls = processor.areScopMonitorControlsVisible();
        const auto showTools = processor.areScopToolsVisible();
        const juce::ScopedValueSetter<bool> controlUpdate(updatingBandControls, true);

        for (size_t bandIndex = 0; bandIndex < bandModeButtons.size(); ++bandIndex)
        {
            const auto isActive = bandIndex < activeBandCount;
            const auto isVisibleBand = isActive
                && (fullSourceView
                    ? bandIndex == 0
                    : (singleViewBand < 0 || singleViewBand == static_cast<int>(bandIndex)));
            const auto selectedMode = processor.getScopChannelMode(bandIndex);
            const auto verticalZoomDecibels = processor.getScopVerticalZoomDecibels(bandIndex);
            const auto normalized = processor.isScopBandNormalized(bandIndex);
            const auto laneBounds = isVisibleBand
                ? getBandBounds(bandIndex, activeBandCount).toNearestInt()
                : juce::Rectangle<int>();
            const auto zoomAvailable = isVisibleBand
                && shouldShowZoomSliders(bandIndex, activeBandCount);
            const auto showVerticalZoomSlider = zoomAvailable && verticalZoomAvailable
                && isVerticalZoomSliderAllowed(bandIndex, activeBandCount);
            const auto controlsY = laneBounds.getY() + ana::ui::gap.pixels();
            const auto normalizeButtonWidth = showTools
                ? bandNormalizeButtons[bandIndex]->getPreferredWidth() : 0;
            const auto preferredZoomControlsWidth = (showTools ? normalizeButtonWidth + ana::ui::gap.pixels() : 0)
                + (showVerticalReadout ? zoomValueWidth : 0)
                + (showVerticalReadout && showVerticalZoomSlider ? ana::ui::gap.pixels() : 0)
                + (showVerticalZoomSlider ? ana::scop::bandZoomSliderWidth : 0);
            const int overlayWidth=laneBounds.getY()==0?ana::ui::analyzer_detail::topRightOverlayWidth(*this):0;
            const int overlayReserve=overlayWidth>0?overlayWidth+(showVerticalZoomSlider?0:ana::scop::bandZoomSliderWidth+ana::ui::gap.pixels()):0;
            const auto leftControlsWidth = std::max(0, getWidth() - preferredZoomControlsWidth-overlayReserve);
            const int leftReserve=ana::ui::analyzer_detail::topLeftOverlayWidth(*this);
            const int leftBottom=ana::ui::analyzer_detail::topLeftOverlayBottom(*this);
            auto controlX = controlsY<leftBottom?leftReserve:0;
            auto controlY = controlsY;
            const auto placeLeftControl = [&] (juce::Component& component, const int width)
            {
                if (controlX + width > leftControlsWidth)
                {
                    controlY += buttonHeight + ana::ui::gap.pixels();
                    controlX = controlY<leftBottom?leftReserve:0;
                }
                component.setBounds(controlX, controlY, width, buttonHeight);
                controlX += width + ana::ui::gap.pixels();
            };

            for (size_t modeIndex = 0; modeIndex < bandModeButtons[bandIndex].size(); ++modeIndex)
            {
                auto& button = *bandModeButtons[bandIndex][modeIndex];
                button.setVisible(isVisibleBand && showMonitorControls && ! fullSourceView);
                button.setToggleState(selectedMode == display::scopModeButtonModes[modeIndex],
                                      juce::dontSendNotification);

                if (isVisibleBand && showMonitorControls && ! fullSourceView)
                    placeLeftControl(button, button.getPreferredWidth());
            }

            auto& clearButton = *bandClearButtons[bandIndex];
            clearButton.setVisible(isVisibleBand && showTools);
            if (isVisibleBand && showTools)
                placeLeftControl(clearButton, clearButton.getPreferredWidth());

            auto& singleViewButton = *bandSingleViewButtons[bandIndex];
            singleViewButton.setVisible(isVisibleBand && showTools && ! fullSourceView);
            singleViewButton.setToggleState(singleViewBand == static_cast<int>(bandIndex),
                                            juce::dontSendNotification);
            if (isVisibleBand && showTools)
                placeLeftControl(singleViewButton, singleViewButton.getPreferredWidth());

            auto& zoomSlider = *bandZoomSliders[bandIndex];
            auto& zoomValueLabel = *bandZoomValueLabels[bandIndex];
            zoomSlider.setVisible(showVerticalZoomSlider);
            zoomValueLabel.setVisible(isVisibleBand && showVerticalReadout);
            zoomSlider.setValue(verticalZoomDecibels, juce::dontSendNotification);
            zoomSlider.setTooltip("ZOOM " + display::formatZoomValue(verticalZoomDecibels));
            if (! zoomValueLabel.isBeingEdited())
                zoomValueLabel.setText(display::formatZoomValue(verticalZoomDecibels),
                                       juce::dontSendNotification);

            auto& normalizeButton = *bandNormalizeButtons[bandIndex];
            normalizeButton.setVisible(isVisibleBand && showTools);
            normalizeButton.setEnabled(isBandNormalizationEnabled(bandIndex));
            normalizeButton.setToggleState(normalized, juce::dontSendNotification);

            refreshBandExtras(bandIndex, laneBounds, isVisibleBand, zoomAvailable,
                              showVerticalZoomSlider, controlY + buttonHeight);

            if (isVisibleBand)
            {
                const auto buttonY = controlsY;
                const auto sliderX = showVerticalZoomSlider
                    ? std::max(0, getWidth() - ana::scop::bandZoomSliderWidth)
                    : getWidth();
                const auto zoomValueX = sliderX
                    - (showVerticalZoomSlider ? ana::ui::gap.pixels() : 0)
                    - zoomValueWidth-overlayReserve;
                if (showVerticalReadout)
                    zoomValueLabel.setBounds(zoomValueX, buttonY, zoomValueWidth, buttonHeight);
                const auto normalizeButtonX = (showVerticalReadout
                    ? zoomValueX - ana::ui::gap.pixels()
                    : sliderX - (showVerticalZoomSlider ? ana::ui::gap.pixels() : 0)-overlayReserve)
                    - normalizeButtonWidth;
                if (showTools)
                    normalizeButton.setBounds(
                        normalizeButtonX, buttonY, normalizeButtonWidth, buttonHeight);

                if (showVerticalZoomSlider)
                {
                    zoomSlider.setBounds(
                        sliderX, buttonY, ana::scop::bandZoomSliderWidth,
                        std::max(1, laneBounds.getBottom()
                            - ana::ui::gap.pixels() - buttonY));
                }
            }
        }
        if (processor.isCleanView())
            ana::ui::hidePlotControls(*this);
    }

    // The same pre-zoom envelope measurement is used by both modes. In LR/MS
    // display modes, the largest absolute peak from either shown channel wins.
    float measureBandPeak(const size_t bandIndex) const
    {
        if (! isBandNormalizationEnabled(bandIndex))
            return 0.0f;

        auto peak = 0.0f;
        const auto displayedModes = display::getDisplayedModes(
            getDisplayedChannelMode(bandIndex));

        for (size_t displayIndex = 0; displayIndex < displayedModes.count; ++displayIndex)
        {
            const auto* envelope = getBandEnvelope(
                bandIndex, displayedModes.indices[displayIndex]);
            if (envelope == nullptr
                || envelope->minimums.empty()
                || envelope->maximums.size() != envelope->minimums.size())
                continue;

            for (size_t index = 0; index < envelope->minimums.size(); ++index)
            {
                const auto minimum = envelope->minimums[index];
                const auto maximum = envelope->maximums[index];
                if (std::isfinite(minimum) && std::isfinite(maximum)
                    && minimum <= maximum)
                    peak = std::max(peak, std::max(std::abs(minimum), std::abs(maximum)));
            }
        }

        return peak;
    }

    // Recompute the zoom from the currently available waveform. An enabled
    // individual normalization remains independent of the new linked mode.
    std::optional<float> calculateNormalizedZoom(const size_t bandIndex) const
    {
        const auto peak = measureBandPeak(bandIndex);
        if (peak <= 1.0e-6f)
            return std::nullopt;

        const auto zoom = juce::Decibels::gainToDecibels(1.0f / peak);
        const auto step = ana::scop::verticalZoomStepDecibels;
        return juce::jlimit(
            ana::scop::minimumVerticalZoomDecibels,
            ana::scop::maximumVerticalZoomDecibels,
            std::round(zoom / step) * step);
    }

    bool applyNormalizedZoom(const size_t bandIndex, const float zoom)
    {
        // Avoid notifying the host on each UI timer tick for the same value.
        if (std::abs(processor.getScopVerticalZoomDecibels(bandIndex) - zoom)
            < ana::scop::verticalZoomStepDecibels * 0.5f)
            return false;

        processor.setScopVerticalZoomDecibels(bandIndex, zoom);
        return true;
    }

    // Shared normalization: find the band whose actual peak is closest to 0 dBFS
    // (the greatest absolute pre-zoom peak), then use ONE dB gain for every band.
    // This preserves their level differences instead of individually levelling them.
    std::optional<float> calculateLinkedNormalizedZoom() const
    {
        if (fullSourceView)
            return std::nullopt;

        auto peak = 0.0f;
        const auto bandCount = processor.getCrossoverCount() + 1;
        for (size_t bandIndex = 0; bandIndex < bandCount; ++bandIndex)
            peak = std::max(peak, measureBandPeak(bandIndex));

        if (peak <= 1.0e-6f)
            return std::nullopt;

        const auto step = ana::scop::verticalZoomStepDecibels;
        const auto zoom = juce::Decibels::gainToDecibels(1.0f / peak);
        return juce::jlimit(
            ana::scop::minimumVerticalZoomDecibels,
            ana::scop::maximumVerticalZoomDecibels,
            std::round(zoom / step) * step);
    }

    void applyLinkedNormalization(bool enable)
    {
        if (!enable)
        {
            processor.setScopLinkedNormalized(false);
            for (size_t i = 0; i < bandNormalizeButtons.size(); ++i)
                applyNormalizedZoom(i, 0.0f);
        }
        else
        {
            // Only one normalization mode can control a band's vertical zoom.
            for (size_t i = 0; i < bandNormalizeButtons.size(); ++i)
                if (processor.isScopBandNormalized(i))
                    processor.setScopBandNormalized(i, false);

            processor.setScopLinkedNormalized(true);
            const auto zoom = calculateLinkedNormalizedZoom().value_or(0.0f);
            for (size_t i = 0; i < bandNormalizeButtons.size(); ++i)
                applyNormalizedZoom(i, zoom);
        }
        refreshBandModeButtons();
        repaint();
    }

    void refreshActiveBandNormalizations()
    {
        bool changed = false;
        const auto activeBandCount = processor.getCrossoverCount() + 1;
        if (processor.isScopLinkedNormalized())
        {
            if (const auto zoom = calculateLinkedNormalizedZoom())
                for (size_t bandIndex = 0; bandIndex < activeBandCount; ++bandIndex)
                    changed = applyNormalizedZoom(bandIndex, *zoom) || changed;
        }
        else
        {
            for (size_t bandIndex = 0; bandIndex < activeBandCount; ++bandIndex)
            {
                if (! processor.isScopBandNormalized(bandIndex)
                    || (fullSourceView && bandIndex != 0))
                    continue;

                if (const auto zoom = calculateNormalizedZoom(bandIndex))
                    changed = applyNormalizedZoom(bandIndex, *zoom) || changed;
            }
        }

        if (changed)
        {
            refreshBandModeButtons();
            repaint();
        }
    }

    void normalizeBandWithZoom(const size_t bandIndex)
    {
        if (! isBandNormalizationEnabled(bandIndex))
            return;

        if (processor.isScopLinkedNormalized())
        {
            processor.setScopLinkedNormalized(false);
            for (size_t i = 0; i < bandNormalizeButtons.size(); ++i)
                applyNormalizedZoom(i, 0.0f);
        }

        if (processor.isScopBandNormalized(bandIndex))
        {
            processor.setScopBandNormalized(bandIndex, false);
            processor.setScopVerticalZoomDecibels(bandIndex, 0.0f);
        }
        else if (const auto zoom = calculateNormalizedZoom(bandIndex))
        {
            processor.setScopBandNormalized(bandIndex, true);
            applyNormalizedZoom(bandIndex, *zoom);
        }

        refreshBandModeButtons();
        repaint();
    }

    juce::Rectangle<float> getBandBounds(
        const size_t bandIndex, const size_t activeBandCount) const noexcept
    {
        return ana::scop::BandLayout {
            getWidth(), getHeight(), minimumBandHeight(), bandHeightWeights,
            fullSourceView ? 0 : singleViewBand
        }.bounds(bandIndex, activeBandCount);
    }

    int findBandSeparator(const int y, const size_t activeBandCount) const noexcept
    {
        if (processor.areScopBandsEqualHeight()) return -1;
        return ana::scop::BandLayout {
            getWidth(), getHeight(), minimumBandHeight(), bandHeightWeights,
            fullSourceView ? 0 : singleViewBand
        }.findSeparator(y, activeBandCount);
    }

    bool shouldShowZoomSliders(
        const size_t bandIndex, const size_t activeBandCount) const noexcept
    {
        if (processor.isCleanView())
            return false;
        juce::ignoreUnused(bandIndex, activeBandCount);
        return true;
    }

    // Vertical zoom is controlled by its Settings switch, not by window
    // height. The editor minimum height reserves space for all SCOP controls.
    bool isVerticalZoomSliderAllowed(
        const size_t bandIndex, const size_t activeBandCount) const noexcept
    {
        return shouldShowZoomSliders(bandIndex, activeBandCount);
    }

    bool hasVisibleZoomSliders() const noexcept
    {
        const auto activeBandCount = processor.getCrossoverCount() + 1;
        if (! processor.areScopVerticalZoomControlsVisible())
            return false;
        for (size_t bandIndex = 0; bandIndex < activeBandCount; ++bandIndex)
            if (isVerticalZoomSliderAllowed(bandIndex, activeBandCount))
                return true;

        return false;
    }

    size_t getWaveformColumnCount() const noexcept
    {
        const auto drawableWidth=getWidth()-ana::scop::waveformRightInset();
        return static_cast<size_t>(std::max(1, drawableWidth));
    }

    PluginProcessor& processor;
    SliderLookAndFeel bandZoomLookAndFeel;
    std::array<std::array<std::unique_ptr<ControlButton>, 6>,
               ana::dsp::LinkwitzRileyCrossover::numBands> bandModeButtons;
    std::array<std::unique_ptr<ControlButton>,
               ana::dsp::LinkwitzRileyCrossover::numBands> bandClearButtons;
    std::array<std::unique_ptr<ControlButton>,
               ana::dsp::LinkwitzRileyCrossover::numBands> bandSingleViewButtons;
    std::array<std::unique_ptr<juce::Slider>,
               ana::dsp::LinkwitzRileyCrossover::numBands> bandZoomSliders;
    std::array<std::unique_ptr<EllipsisLabel>,
               ana::dsp::LinkwitzRileyCrossover::numBands> bandZoomValueLabels;
    std::array<std::unique_ptr<ControlButton>,
               ana::dsp::LinkwitzRileyCrossover::numBands> bandNormalizeButtons;
    std::array<float, ana::dsp::LinkwitzRileyCrossover::numBands> bandHeightWeights;
    bool updatingBandControls = false;
    bool fullSourceView = false;
    int singleViewBand = -1;
    int draggedBandSeparator = -1;
};
}
