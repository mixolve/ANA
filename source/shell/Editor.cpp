#include "Editor.h"
#include "Processor.h"
#include "Theme.h"
#if ANA_HAS_SF_SYMBOLS
#include "SfSymbols.h"
#endif

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
constexpr float minimumCrossoverGapHz = 1.0f;
constexpr int bandZoomValueWidth = ana::ui::textControlWidth(6);
constexpr int bandRangeSliderHeight = 14;
constexpr int bandZoomSliderWidth = bandRangeSliderHeight;
constexpr int minimumBandHeight = ana::ui::gap.pixels() * 3 + ana::ui::controlHeight + bandRangeSliderHeight;
constexpr int minimumEditorHeight = 300;
constexpr int maximumEditorSize = 32768;
constexpr int readoutTextVerticalOffset = -1;
constexpr int waveformRightInset = ana::ui::gap.pixels() + bandZoomSliderWidth;
constexpr int levelScaleLabelWidth = ana::ui::textControlWidth(3);
constexpr int levelMeterReadoutWidth = ana::ui::textControlWidth(7);
constexpr int historyMetricReadoutWidth = ana::ui::textControlWidth(7);
constexpr int historySviewWidth = ana::ui::textControlWidth(5);
constexpr int historyMinimumWidth = historyMetricReadoutWidth * 2 + historySviewWidth + bandZoomSliderWidth
    + ana::ui::gap.pixels() * 3;
constexpr int minimumEditorWidth = 800;
constexpr int defaultEditorWidth = minimumEditorWidth;
constexpr int defaultEditorHeight = minimumEditorHeight;
constexpr std::array<const char*, 6> scopeModeButtonNames { "LR", "L", "R", "MS", "M", "S" };
constexpr std::array<ana::ScopeChannelMode, 6> scopeModeButtonModes {
    ana::ScopeChannelMode::lr,
    ana::ScopeChannelMode::left,
    ana::ScopeChannelMode::right,
    ana::ScopeChannelMode::ms,
    ana::ScopeChannelMode::mid,
    ana::ScopeChannelMode::side
};

class InvisibleResizableEdgeComponent final : public juce::ResizableEdgeComponent
{
public:
    using juce::ResizableEdgeComponent::ResizableEdgeComponent;
    void paint(juce::Graphics&) override {}
};

struct LevelHistoryLayout
{
    juce::Rectangle<int> header;
    juce::Rectangle<int> plot;
    juce::Rectangle<int> metricLabelRow;
    juce::Rectangle<int> metricReadoutRow;
    juce::Rectangle<int> sviewButton;
    juce::Rectangle<int> horizontalZoom;
    juce::Rectangle<int> verticalZoom;
};

LevelHistoryLayout makeLevelHistoryLayout(juce::Rectangle<int> bounds,
                                          const bool showZoomControls) noexcept
{
    LevelHistoryLayout layout;
    auto historyArea = bounds;
    if (showZoomControls)
    {
        layout.verticalZoom = historyArea.removeFromRight(
            std::min(bandZoomSliderWidth, historyArea.getWidth()));
        ana::ui::gap.removeFromRight(historyArea);
    }

    layout.header = historyArea.removeFromTop(
        std::min(ana::ui::controlHeight, historyArea.getHeight()));
    ana::ui::gap.removeFromTop(historyArea);

    auto plotArea = historyArea;
    if (showZoomControls)
    {
        layout.horizontalZoom = plotArea.removeFromBottom(
            std::min(bandRangeSliderHeight, plotArea.getHeight()));
        ana::ui::gap.removeFromBottom(plotArea);
    }
    layout.plot = plotArea;

    const auto sviewWidth = std::min(historySviewWidth, std::max(0, layout.plot.getWidth()));
    const auto sviewY = std::max(layout.plot.getY(),
        layout.plot.getBottom() - ana::ui::controlHeight);
    layout.sviewButton = {
        layout.plot.getX(),
        sviewY,
        sviewWidth,
        std::min(ana::ui::controlHeight, layout.plot.getBottom() - sviewY)
    };
    layout.metricLabelRow = historyArea.withWidth(historyMetricReadoutWidth * 2
                                             + ana::ui::gap.pixels())
                                      .withHeight(std::min(ana::ui::controlHeight,
                                                          historyArea.getHeight()));
    layout.metricReadoutRow = historyArea.withWidth(historyMetricReadoutWidth * 2
                                               + ana::ui::gap.pixels())
        .withHeight(layout.metricLabelRow.getHeight())
        .withY(layout.metricLabelRow.getBottom() + ana::ui::gap.pixels());
    return layout;
}

struct DisplayedModes
{
    std::array<size_t, 2> indices {};
    size_t count = 1;
};

DisplayedModes getDisplayedModes(const ana::ScopeChannelMode mode) noexcept
{
    switch (mode)
    {
        case ana::ScopeChannelMode::lr: return { { 0, 1 }, 2 };
        case ana::ScopeChannelMode::ms: return { { 2, 3 }, 2 };
        case ana::ScopeChannelMode::left: return { { 0, 0 }, 1 };
        case ana::ScopeChannelMode::right: return { { 1, 1 }, 1 };
        case ana::ScopeChannelMode::mid: return { { 2, 2 }, 1 };
        case ana::ScopeChannelMode::side: return { { 3, 3 }, 1 };
    }

    return { { 2, 2 }, 1 };
}

juce::String formatFrequency(const double frequency)
{
    return juce::String(frequency, frequency >= 100.0 ? 0 : 1);
}

juce::String formatReadoutFrequency(const double frequency)
{
    return juce::String::formatted("%08.2f", frequency);
}

juce::String formatReadoutLevel(const double level)
{
    return juce::String::formatted("%+07.2f", level);
}

juce::String formatLufsReadout(const double level)
{
    return level <= -119.9 ? juce::String("-inf") : formatReadoutLevel(level);
}

juce::String formatLevelScaleTick(const int value)
{
    return juce::String::formatted("%+03d", value);
}

juce::String formatCorrelationCoefficient(const double coefficient)
{
    return juce::String::formatted("%+.2f", coefficient);
}

juce::String formatBlockSizeChoice(const double value)
{
    constexpr std::array<int, 6> sizes { 512, 1024, 2048, 4096, 8192, 16384 };
    return juce::String(sizes[static_cast<size_t>(juce::jlimit(
        0, static_cast<int>(sizes.size()) - 1, juce::roundToInt(value)))]);
}

juce::String formatSpectrumTypeChoice(const double value)
{
    constexpr std::array<const char*, 2> choices { "RTAVG", "MAX" };
    return choices[static_cast<size_t>(juce::jlimit(
        0, static_cast<int>(choices.size()) - 1, juce::roundToInt(value)))];
}

juce::Rectangle<int> readoutTextBounds(const juce::Rectangle<int> bounds) noexcept
{
    return bounds.translated(0, readoutTextVerticalOffset);
}

double parseFrequency(const juce::String& text)
{
    const auto trimmed = text.trim().toLowerCase();
    const auto multiplier = trimmed.containsChar('k') ? 1000.0 : 1.0;
    return trimmed.getDoubleValue() * multiplier;
}

juce::String formatZoomValue(const float decibels)
{
    const auto displayValue = std::abs(decibels) < 0.05f ? 0.0f : decibels;
    return (displayValue > 0.0f ? "+" : "") + juce::String(displayValue, 2);
}

void drawWaveformEnvelope(juce::Graphics& graphics,
                          const std::vector<float>& minimums,
                          const std::vector<float>& maximums,
                          const juce::Rectangle<float> bounds,
                          const float rangeStart,
                          const float rangeEnd,
                          const float verticalZoomDecibels,
                          const bool filledStyle)
{
    if (minimums.empty() || maximums.size() != minimums.size() || bounds.isEmpty())
        return;

    const auto sourceColumns = minimums.size();
    const auto visibleStart = juce::jlimit(0.0f, 0.999f, rangeStart);
    const auto visibleEnd = juce::jlimit(visibleStart + 0.001f, 1.0f, rangeEnd);
    const auto firstSourcePosition = visibleStart * static_cast<float>(sourceColumns - 1);
    const auto lastSourcePosition = visibleEnd * static_cast<float>(sourceColumns - 1);
    const auto visibleSourceLength = std::max(0.001f,
        lastSourcePosition - firstSourcePosition);
    const auto firstSourceIndex = std::min(
        sourceColumns - 1,
        static_cast<size_t>(std::floor(firstSourcePosition)));
    const auto lastSourceIndex = std::min(
        sourceColumns - 1,
        static_cast<size_t>(std::ceil(lastSourcePosition)));
    const auto centreY = bounds.getCentreY();
    const auto amplitude = std::max(0.0f, bounds.getHeight() * 0.5f);
    const auto zoomGain = juce::Decibels::decibelsToGain(verticalZoomDecibels);
    juce::Graphics::ScopedSaveState clipState(graphics);
    graphics.reduceClipRegion(bounds.toNearestInt());
    juce::Path path;
    std::vector<juce::Point<float>> lowerEdge;
    auto pointCount = 0;
    auto singleTop = 0.0f;

    const auto flushPath = [&]
    {
        if (pointCount == 1)
        {
            graphics.drawLine(lowerEdge.front().x, singleTop,
                              lowerEdge.front().x, lowerEdge.front().y, 1.0f);
        }
        else if (pointCount > 1)
        {
            for (auto iterator = lowerEdge.rbegin(); iterator != lowerEdge.rend(); ++iterator)
                path.lineTo(*iterator);

            path.closeSubPath();
            if (filledStyle)
                graphics.fillPath(path);
            else
                graphics.strokePath(path, juce::PathStrokeType(1.25f));
        }

        path.clear();
        lowerEdge.clear();
        pointCount = 0;
    };

    for (auto sourceIndex = firstSourceIndex;
         sourceIndex <= lastSourceIndex;
         ++sourceIndex)
    {
        auto minimum = minimums[sourceIndex];
        auto maximum = maximums[sourceIndex];

        if (minimum > maximum)
        {
            flushPath();
            continue;
        }

        minimum *= zoomGain;
        maximum *= zoomGain;
        const auto normalizedX =
            (static_cast<float>(sourceIndex) - firstSourcePosition) / visibleSourceLength;
        const auto x = bounds.getX() + normalizedX * bounds.getWidth();
        const auto top = centreY - maximum * amplitude;
        const auto bottom = centreY - minimum * amplitude;

        if (pointCount == 0)
        {
            path.startNewSubPath(x, top);
            singleTop = top;
        }
        else
        {
            path.lineTo(x, top);
        }

        lowerEdge.emplace_back(x, bottom);
        ++pointCount;
    }

    flushPath();
}

} // namespace

void EllipsisLabel::paint(juce::Graphics& graphics)
{
    if (isBeingEdited())
    {
        getLookAndFeel().drawLabel(graphics, *this);
        return;
    }

    if (drawBackground)
        graphics.fillAll(findColour(juce::Label::backgroundColourId));
    graphics.setColour(isEnabled() ? findColour(juce::Label::textColourId)
                                   : ana::ui::light);
    graphics.setFont(getFont());
    graphics.drawText(getText(), getBorderSize().subtractedFrom(getLocalBounds())
                                     .translated(0, textVerticalOffset),
                      getJustificationType(), true);
    graphics.setColour(isEnabled() ? findColour(juce::Label::outlineColourId)
                                   : ana::ui::light);
    graphics.drawRect(getLocalBounds());
}

ControlButton::ControlButton(juce::String text)
    : juce::Button(std::move(text))
{
    iconButton = getButtonText() == "chevron.forward.2"
        || getButtonText() == "gearshape"
        || getButtonText() == "snowflake"
        || getButtonText() == "arrow.trianglehead.2.clockwise"
        || getButtonText() == "info.circle";
#if ANA_HAS_SF_SYMBOLS
    if (iconButton)
        symbolImage = loadSfSymbol(getButtonText(), ana::ui::iconFontSize);
#endif
    setWantsKeyboardFocus(false);
}

int ControlButton::getPreferredWidth() const noexcept
{
    return iconButton ? ana::ui::iconControlSize : ana::ui::textControlWidth(getButtonText());
}

void ControlButton::paintButton(juce::Graphics& graphics,
                                 const bool shouldDrawButtonAsHighlighted,
                                 const bool shouldDrawButtonAsDown)
{
    const auto bounds = getLocalBounds();
    const auto hovered = isEnabled()
        && (shouldDrawButtonAsHighlighted || shouldDrawButtonAsDown);
    const auto fill = hovered ? ana::ui::hover : ana::ui::field;

    graphics.setColour(fill);
    graphics.fillRect(bounds);
    const auto active = isEnabled() && getToggleState();
    graphics.setColour(active ? ana::ui::white : ana::ui::grey500);
    graphics.drawRect(bounds, active ? ana::ui::activeBorderWidth : 1);
    const auto foreground = ! isEnabled() ? ana::ui::grey500
        : hovered ? juce::Colours::black : ana::ui::white;
    graphics.setColour(foreground);
    if (symbolImage.isValid())
    {
        juce::DrawableImage drawable;
        drawable.setImage(symbolImage);
        drawable.setOverlayColour(foreground);
        const auto iconSize = juce::roundToInt(ana::ui::iconFontSize);
        drawable.drawWithin(graphics, getLocalBounds().withSizeKeepingCentre(iconSize, iconSize).toFloat(),
                            juce::RectanglePlacement::centred, 1.0f);
    }
    else
    {
        graphics.setFont(ana::ui::makeFont());
        graphics.drawText(getButtonText(), getLocalBounds().reduced(ana::ui::gap.pixels(), 1),
                          juce::Justification::centred, true);
    }
}

AboutPopup::AboutPopup(std::function<void()> closeCallback)
    : onClose(std::move(closeCallback))
{
    setOpaque(true);
    setWantsKeyboardFocus(true);
    setMouseClickGrabsKeyboardFocus(false);

    const auto configureLink = [this] (juce::HyperlinkButton& link)
    {
        link.setFont(ana::ui::makeFont(), false);
        link.setJustificationType(juce::Justification::centred);
        link.setColour(juce::HyperlinkButton::textColourId, ana::ui::accent);
        link.setWantsKeyboardFocus(false);
        addAndMakeVisible(link);
    };
    configureLink(webLink);
    configureLink(manualLink);
    manualLink.onClick = []
    {
        const auto url = createOfflineManualUrl();
        if (url.isWellFormed())
            url.launchInDefaultBrowser();
    };

    const auto aboutText = juce::String::fromUTF8(BinaryData::about_md, BinaryData::about_mdSize);
    for (const auto& line : juce::StringArray::fromLines(aboutText))
    {
        const auto trimmed = line.trim();
        if (trimmed.isEmpty())
            continue;
        if (trimmed.startsWith("[WEB]"))
        {
            contentRows.push_back(&webLink);
            continue;
        }
        if (trimmed.startsWith("[MANUAL]"))
        {
            contentRows.push_back(&manualLink);
            continue;
        }

        auto label = std::make_unique<EllipsisLabel>();
        label->setText(trimmed, juce::dontSendNotification);
        label->setFont(ana::ui::makeFont());
        label->setJustificationType(juce::Justification::centred);
        label->setColour(juce::Label::textColourId, ana::ui::white);
        label->setColour(juce::Label::backgroundColourId, ana::ui::black);
        label->setColour(juce::Label::outlineColourId, ana::ui::black);
        addAndMakeVisible(*label);
        contentRows.push_back(label.get());
        textLabels.push_back(std::move(label));
    }

    okButton.onClick = [this] { requestClose(); };
    addAndMakeVisible(okButton);
}

void AboutPopup::paint(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colours::black);
}

void AboutPopup::resized()
{
    auto area = getLocalBounds().reduced(ana::ui::gap.pixels());
    okButton.setBounds(area.removeFromBottom(ana::ui::controlHeight));
    ana::ui::gap.removeFromBottom(area);
    for (size_t index = 0; index < contentRows.size(); ++index)
    {
        contentRows[index]->setBounds(area.removeFromTop(ana::ui::controlHeight));
        if (index + 1 < contentRows.size())
            ana::ui::gap.removeFromTop(area);
    }
}

bool AboutPopup::keyPressed(const juce::KeyPress& key)
{
    if (key != juce::KeyPress::escapeKey)
        return false;
    requestClose();
    return true;
}

void AboutPopup::requestClose()
{
    auto deferredClose = std::move(onClose);
    onClose = {};
    juce::MessageManager::callAsync([callback = std::move(deferredClose)]
    {
        if (callback != nullptr)
            callback();
    });
}

juce::URL AboutPopup::createOfflineManualUrl()
{
    const auto directory = juce::File::getSpecialLocation(juce::File::tempDirectory)
                               .getChildFile("mixolve-ana");
    if (directory.createDirectory().failed())
        return {};
    const auto manualFile = directory.getChildFile("manual.md");
    manualFile.setReadOnly(false);
    if (! manualFile.replaceWithData(BinaryData::manual_md,
                                     static_cast<size_t>(BinaryData::manual_mdSize)))
        return {};
    manualFile.setReadOnly(true);
    return juce::URL(manualFile);
}

juce::Slider::SliderLayout SliderLookAndFeel::getSliderLayout(juce::Slider& slider)
{
    if (slider.getSliderStyle() == juce::Slider::LinearBarVertical)
        return { slider.getLocalBounds(), {} };

    return juce::LookAndFeel_V4::getSliderLayout(slider);
}

void SliderLookAndFeel::drawLinearSlider(juce::Graphics& graphics,
                                            const int x, const int y, const int width, const int height,
                                            const float sliderPosition,
                                            const float minimumSliderPosition,
                                            const float maximumSliderPosition,
                                            const juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style == juce::Slider::LinearBarVertical)
    {
        const auto frameBounds = juce::Rectangle<int>(x, y, width, height);
        const auto bounds = frameBounds.toFloat();
        const auto markerY = juce::jlimit(bounds.getY(), bounds.getBottom(), sliderPosition);
        const auto neutralProportion = static_cast<float>(slider.valueToProportionOfLength(0.0));
        const auto neutralY = juce::jmap(neutralProportion, bounds.getBottom(), bounds.getY());
        const auto fillTop = std::min(markerY, neutralY);
        const auto fillBottom = std::max(markerY, neutralY);
        graphics.setColour(ana::ui::field);
        graphics.fillRect(bounds);
        graphics.setColour(slider.isEnabled() ? ana::ui::light : ana::ui::dark);
        graphics.fillRect(bounds.withTop(fillTop).withBottom(fillBottom));
        graphics.setColour(ana::ui::grey500);
        graphics.fillRect(bounds.getX(), neutralY - 0.5f, bounds.getWidth(), 1.0f);
        graphics.setColour(slider.isEnabled() ? ana::ui::accent : ana::ui::grey500);
        graphics.fillRect(bounds.getX(), markerY - 1.0f, bounds.getWidth(), 2.0f);
        graphics.setColour(ana::ui::grey500);
        graphics.drawRect(frameBounds, 1);
        return;
    }

    if (style != juce::Slider::LinearHorizontal && style != juce::Slider::LinearBar)
    {
        juce::LookAndFeel_V4::drawLinearSlider(graphics, x, y, width, height,
                                               sliderPosition, minimumSliderPosition, maximumSliderPosition,
                                               style, slider);
        return;
    }

    const auto frameBounds = juce::Rectangle<int>(x, y, width, height);
    const auto bounds = frameBounds.toFloat();
    const auto markerX = juce::jlimit(bounds.getX(), bounds.getRight(), sliderPosition);

    graphics.setColour(ana::ui::field);
    graphics.fillRect(bounds);
    graphics.setColour(slider.isEnabled() ? ana::ui::light : ana::ui::dark);
    graphics.fillRect(bounds.withRight(markerX));
    graphics.setColour(slider.isEnabled() ? ana::ui::accent : ana::ui::grey500);
    graphics.fillRect(markerX - 1.0f, bounds.getY(), 2.0f, bounds.getHeight());
    graphics.setColour(ana::ui::grey500);
    graphics.drawRect(frameBounds, 1);

    if (style == juce::Slider::LinearBar)
    {
        graphics.setColour(slider.isEnabled() ? ana::ui::white : ana::ui::grey500);
        graphics.setFont(ana::ui::makeFont());
        graphics.drawText(slider.getTextFromValue(slider.getValue()),
                          readoutTextBounds(bounds.toNearestInt().reduced(ana::ui::gap.pixels(), 1)),
                          juce::Justification::centred, true);
    }
}

void SliderLookAndFeel::drawScrollbar(juce::Graphics& graphics,
                                         juce::ScrollBar&,
                                         const int x, const int y, const int width, const int height,
                                         const bool isScrollbarVertical,
                                         const int thumbStartPosition,
                                         const int thumbSize,
                                         const bool,
                                         const bool isMouseDown)
{
    const auto bounds = juce::Rectangle<int>(x, y, width, height);
    graphics.setColour(ana::ui::field);
    graphics.fillRect(bounds);
    graphics.setColour(ana::ui::grey500);
    graphics.drawRect(bounds, 1);

    if (thumbSize <= 0)
        return;

    auto thumb = isScrollbarVertical
        ? juce::Rectangle<int>(x + 2, thumbStartPosition, std::max(1, width - 4), thumbSize)
        : juce::Rectangle<int>(thumbStartPosition, y + 2, thumbSize, std::max(1, height - 4));
    graphics.setColour(isMouseDown ? ana::ui::white : ana::ui::light);
    graphics.fillRect(thumb);
}

juce::Label* SliderLookAndFeel::createSliderTextBox(juce::Slider& slider)
{
    auto* label = juce::LookAndFeel_V4::createSliderTextBox(slider);
    label->setFont(ana::ui::makeFont());
    label->setJustificationType(juce::Justification::centred);
    label->setColour(juce::Label::backgroundColourId, ana::ui::field);
    label->setColour(juce::Label::outlineColourId, ana::ui::grey500);
    label->setColour(juce::Label::textColourId, ana::ui::white);
    label->setColour(juce::Label::textWhenEditingColourId, ana::ui::white);
    return label;
}

ParameterControl::ParameterControl(juce::AudioProcessorValueTreeState& state,
                                         const juce::String& parameterId,
                                         juce::String title,
                                         Formatter formatter)
    : titleText(std::move(title)), valueFormatter(std::move(formatter)), compact(titleText.isEmpty())
{
    parameter = state.getParameter(parameterId);
    choiceParameter = dynamic_cast<juce::AudioParameterChoice*>(parameter);
    boolParameter = dynamic_cast<juce::AudioParameterBool*>(parameter);
    setLookAndFeel(&sliderLookAndFeel);
    slider.setLookAndFeel(&sliderLookAndFeel);
    slider.setSliderStyle(compact ? juce::Slider::LinearBar : juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(compact ? juce::Slider::NoTextBox : juce::Slider::TextBoxRight,
                           false, compact ? 0 : 116, 34);
    slider.setScrollWheelEnabled(false);
    slider.setWantsKeyboardFocus(false);
    slider.setMouseClickGrabsKeyboardFocus(false);
    slider.setInterceptsMouseClicks(false, false);
    slider.textFromValueFunction = [this] (const double value)
    {
        return valueFormatter != nullptr ? valueFormatter(value) : juce::String(value);
    };
    slider.setColour(juce::Slider::textBoxTextColourId, ana::ui::white);
    slider.setColour(juce::Slider::textBoxBackgroundColourId, ana::ui::field);
    slider.setColour(juce::Slider::textBoxOutlineColourId, ana::ui::grey500);
    slider.onValueChange = [this]
    {
        repaint();

        if (onValueChanged)
            onValueChanged();
    };
    addChildComponent(slider);
    valueEditor.setFont(ana::ui::makeFont());
    valueEditor.setJustification(juce::Justification::centred);
    valueEditor.setPopupMenuEnabled(false);
    valueEditor.setSelectAllWhenFocused(true);
    valueEditor.setColour(juce::TextEditor::textColourId, ana::ui::white);
    valueEditor.setColour(juce::TextEditor::backgroundColourId, ana::ui::field);
    valueEditor.setColour(juce::TextEditor::outlineColourId, ana::ui::grey500);
    valueEditor.setColour(juce::TextEditor::focusedOutlineColourId, ana::ui::grey500);
    valueEditor.setColour(juce::TextEditor::highlightColourId, ana::ui::black);
    valueEditor.setColour(juce::TextEditor::highlightedTextColourId, ana::ui::white);
    valueEditor.onReturnKey = [this] { hideValueEditor(false); };
    valueEditor.onEscapeKey = [this] { hideValueEditor(true); };
    valueEditor.onFocusLost = [this]
    {
        if (valueEditorActive)
            hideValueEditor(false);
    };
    addChildComponent(valueEditor);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(state, parameterId, slider);
    slider.textFromValueFunction = [this] (const double value)
    {
        return valueFormatter != nullptr ? valueFormatter(value) : juce::String(value);
    };
}

ParameterControl::~ParameterControl()
{
    stopTimer();
    slider.setLookAndFeel(nullptr);
    setLookAndFeel(nullptr);
}

void ParameterControl::setInteractionEnabled(const bool shouldEnable)
{
    if (interactionEnabled == shouldEnable)
        return;

    interactionEnabled = shouldEnable;

    if (! interactionEnabled)
    {
        selected = false;
        hideValueEditor(true);
    }

    repaint();
}

juce::StringArray ParameterControl::getChoiceNames() const
{
    return choiceParameter != nullptr ? choiceParameter->choices : juce::StringArray();
}

int ParameterControl::getSelectedChoiceIndex() const noexcept
{
    return choiceParameter != nullptr ? juce::roundToInt(slider.getValue()) : -1;
}

void ParameterControl::setSelectedChoiceIndex(const int choiceIndex)
{
    if (choiceParameter == nullptr || ! juce::isPositiveAndBelow(choiceIndex, choiceParameter->choices.size()))
        return;

    slider.setValue(choiceIndex, juce::sendNotificationSync);
}

void ParameterControl::setSelected(const bool shouldSelect)
{
    const auto nextSelected = shouldSelect && interactionEnabled;

    if (selected == nextSelected)
        return;

    selected = nextSelected;
    repaint();
}

void ParameterControl::setCompact(const bool shouldUseCompactLayout)
{
    if (compact == shouldUseCompactLayout)
        return;

    compact = shouldUseCompactLayout;
    resized();
    repaint();
}

void ParameterControl::paint(juce::Graphics& graphics)
{
    const auto titleHovered = interactionEnabled && hoverRegion == PressRegion::title;
    const auto valueHovered = interactionEnabled && hoverRegion == PressRegion::value;
    graphics.setColour(titleHovered ? ana::ui::hover : ana::ui::field);
    graphics.fillRect(titleBounds);
    graphics.setColour(valueHovered ? ana::ui::hover : ana::ui::field);
    graphics.fillRect(valueBounds);
    graphics.setColour(selected ? ana::ui::white : ana::ui::grey500);
    graphics.drawRect(titleBounds, selected ? ana::ui::activeBorderWidth : 1);
    graphics.setColour(ana::ui::grey500);
    graphics.drawRect(valueBounds, 1);
    graphics.setFont(ana::ui::makeFont());
    graphics.setColour(! interactionEnabled ? ana::ui::grey500
                       : titleHovered ? juce::Colours::black : ana::ui::white);
    graphics.drawText(longPressArmed ? "RESET?" : titleText,
                      titleBounds.reduced(ana::ui::textPadding, 1),
                      juce::Justification::centredLeft, true);
    graphics.setColour(! interactionEnabled ? ana::ui::grey500
                       : valueHovered ? juce::Colours::black : ana::ui::white);
    graphics.drawText(interactionEnabled ? slider.getTextFromValue(slider.getValue()) : "OFF",
                      readoutTextBounds(valueBounds.reduced(ana::ui::gap.pixels(), 1)),
                      juce::Justification::centred, true);
}

void ParameterControl::resized()
{
    auto row = getLocalBounds();

    if (! compact)
    {
        const auto availableWidth = std::max(0, row.getWidth() - ana::ui::gap.pixels());
        titleBounds = row.removeFromLeft(availableWidth / 2);
        ana::ui::gap.removeFromLeft(row);
        valueBounds = row;
    }
    else
    {
        titleBounds = {};
        valueBounds = row;
    }
    slider.setBounds(valueBounds);
    valueEditor.setBounds(valueBounds);
}

void ParameterControl::mouseDown(const juce::MouseEvent& event)
{
    if (! interactionEnabled || ! event.mods.isLeftButtonDown())
        return;

    pointerDown = true;
    dragDetected = false;
    longPressArmed = false;
    pressHighlighted = true;
    pressRegion = titleBounds.contains(event.getPosition()) ? PressRegion::title
                  : valueBounds.contains(event.getPosition()) ? PressRegion::value
                                                               : PressRegion::none;

    if (pressRegion == PressRegion::title && parameter != nullptr)
        startTimer(500);

    repaint();
}

void ParameterControl::mouseMove(const juce::MouseEvent& event)
{
    const auto nextHoverRegion = ! interactionEnabled ? PressRegion::none
        : titleBounds.contains(event.getPosition()) ? PressRegion::title
        : valueBounds.contains(event.getPosition()) ? PressRegion::value
                                                    : PressRegion::none;
    if (hoverRegion != nextHoverRegion)
    {
        hoverRegion = nextHoverRegion;
        repaint();
    }
}

void ParameterControl::mouseDrag(const juce::MouseEvent& event)
{
    if (! pointerDown)
        return;

    if (event.mouseWasDraggedSinceMouseDown())
    {
        dragDetected = true;
        longPressArmed = false;
        pressHighlighted = false;
        stopTimer();
        repaint();
    }
}

void ParameterControl::mouseUp(const juce::MouseEvent& event)
{
    if (! pointerDown)
        return;

    stopTimer();
    const auto releasedRegion = pressRegion;
    const auto wasLongPress = longPressArmed;
    const auto shouldActivate = ! dragDetected
        && ((releasedRegion == PressRegion::title && titleBounds.contains(event.getPosition()))
            || (releasedRegion == PressRegion::value && valueBounds.contains(event.getPosition())));
    pointerDown = false;
    dragDetected = false;
    pressHighlighted = false;
    longPressArmed = false;
    pressRegion = PressRegion::none;
    repaint();

    if (! shouldActivate)
        return;

    if (releasedRegion == PressRegion::title)
    {
        if (wasLongPress)
            resetToDefault();
        else if (onFocusRequested)
            onFocusRequested(*this);

        return;
    }

    if (choiceParameter != nullptr)
    {
        if (onChoiceRequested)
            onChoiceRequested(*this);
    }
    else if (boolParameter != nullptr)
    {
        slider.setValue(slider.getValue() < 0.5 ? 1.0 : 0.0, juce::sendNotificationSync);
    }
    else
    {
        if (onFocusRequested)
            onFocusRequested(*this);

        showValueEditor();
    }
}

void ParameterControl::commitPendingEditor()
{
    hideValueEditor(false);
}

void ParameterControl::mouseExit(const juce::MouseEvent&)
{
    hoverRegion = PressRegion::none;

    if (! pointerDown)
    {
        repaint();
        return;
    }

    pressHighlighted = false;
    longPressArmed = false;
    stopTimer();
    repaint();
}

void ParameterControl::timerCallback()
{
    stopTimer();

    if (! pointerDown || dragDetected || pressRegion != PressRegion::title || ! pressHighlighted)
        return;

    longPressArmed = true;
    repaint();
}

void ParameterControl::showValueEditor()
{
    if (! interactionEnabled || choiceParameter != nullptr || valueEditorActive)
        return;

    valueEditorActive = true;
    valueEditor.setText(slider.getTextFromValue(slider.getValue()), false);
    valueEditor.setVisible(true);
    valueEditor.toFront(false);
    valueEditor.grabKeyboardFocus();
    valueEditor.selectAll();
}

void ParameterControl::hideValueEditor(const bool discardChanges)
{
    if (! valueEditorActive)
        return;

    const auto enteredText = valueEditor.getText();
    valueEditorActive = false;
    valueEditor.setVisible(false);

    if (! discardChanges)
    {
        const auto enteredValue = slider.getValueFromText(enteredText);
        slider.setValue(juce::jlimit(slider.getMinimum(), slider.getMaximum(), enteredValue),
                        juce::sendNotificationSync);
    }

    repaint();
}

void ParameterControl::resetToDefault()
{
    if (parameter == nullptr || ! interactionEnabled)
        return;

    slider.setValue(parameter->convertFrom0to1(parameter->getDefaultValue()),
                    juce::sendNotificationSync);
}

ChoicePopup::ChoicePopup(juce::Rectangle<int> anchorBoundsIn,
                                 juce::StringArray choicesIn,
                                 const int selectedIndex,
                                 std::function<void(int)> selectCallback,
                                 std::function<void()> closeCallback)
    : anchorBounds(anchorBoundsIn),
      choices(std::move(choicesIn)),
      onSelect(std::move(selectCallback)),
      onClose(std::move(closeCallback))
{
    setOpaque(false);
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);
    setInterceptsMouseClicks(true, true);
    choiceButtons.reserve(static_cast<size_t>(choices.size()));

    for (int index = 0; index < choices.size(); ++index)
    {
        auto button = std::make_unique<ControlButton>(choices[index]);
        button->setToggleState(choices.size() != 2 && index == selectedIndex,
                               juce::dontSendNotification);
        button->onClick = [this, index] { choose(index); };
        addAndMakeVisible(*button);
        choiceButtons.push_back(std::move(button));
    }
}

void ChoicePopup::paintOverChildren(juce::Graphics& graphics)
{
    graphics.setColour(ana::ui::white);
    graphics.drawRect(panelBounds, 2);
}

void ChoicePopup::resized()
{
    constexpr int popupItemGap = 0;
    const auto itemCount = static_cast<int>(choiceButtons.size());
    const auto contentHeight = itemCount * ana::ui::controlHeight
        + std::max(0, itemCount - 1) * popupItemGap;
    auto choiceWidth = ana::ui::textControlWidth(1);
    for (const auto& choice : choices)
        choiceWidth = std::max(choiceWidth, ana::ui::textControlWidth(choice));
    const auto desiredPanelWidth = choiceWidth;
    const auto panelWidth = std::max(1, std::min(
        std::max(anchorBounds.getWidth(), desiredPanelWidth),
        getWidth() - ana::ui::gap.pixels() * 2));
    const auto panelHeight = std::min(getHeight() - ana::ui::gap.pixels() * 2,
                                      contentHeight);
    panelBounds = juce::Rectangle<int>(panelWidth, std::max(1, panelHeight));
    panelBounds.setPosition(anchorBounds.getPosition());
    panelBounds = panelBounds.constrainedWithin(getLocalBounds().reduced(ana::ui::gap.pixels()));

    auto area = panelBounds;
    for (size_t index = 0; index < choiceButtons.size(); ++index)
    {
        const auto row = area.removeFromTop(ana::ui::controlHeight);
        choiceButtons[index]->setBounds(row);

        if (index + 1 < choiceButtons.size())
            area.removeFromTop(popupItemGap);
    }
}

void ChoicePopup::mouseDown(const juce::MouseEvent& event)
{
    if (! panelBounds.contains(event.getPosition()))
        close();
}

void ChoicePopup::choose(const int index)
{
    if (closing)
        return;

    closing = true;
    auto selectCallback = std::move(onSelect);
    auto closeCallback = std::move(onClose);
    juce::MessageManager::callAsync([index,
                                     select = std::move(selectCallback),
                                     closePrompt = std::move(closeCallback)]() mutable
    {
        if (select)
            select(index);

        if (closePrompt)
            closePrompt();
    });
}

void ChoicePopup::close()
{
    if (closing)
        return;

    closing = true;
    auto closeCallback = std::move(onClose);
    juce::MessageManager::callAsync([closePrompt = std::move(closeCallback)]() mutable
    {
        if (closePrompt)
            closePrompt();
    });
}

void RangeSlider::paint(juce::Graphics& graphics)
{
    const auto frameBounds = getLocalBounds();
    const auto bounds = frameBounds.toFloat();

    graphics.setColour(ana::ui::field);
    graphics.fillRect(bounds);
    graphics.setColour(ana::ui::light);

    if (orientation == Orientation::horizontal)
    {
        const auto startX = juce::jmap(rangeStart, bounds.getX(), bounds.getRight());
        const auto endX = juce::jmap(rangeEnd, bounds.getX(), bounds.getRight());
        graphics.fillRect(bounds.withLeft(startX).withRight(endX));
        graphics.setColour(ana::ui::accent);
        graphics.fillRect(startX - 1.0f, bounds.getY(), 2.0f, bounds.getHeight());
        graphics.fillRect(endX - 1.0f, bounds.getY(), 2.0f, bounds.getHeight());
    }
    else
    {
        const auto startY = juce::jmap(rangeStart, bounds.getY(), bounds.getBottom());
        const auto endY = juce::jmap(rangeEnd, bounds.getY(), bounds.getBottom());
        graphics.fillRect(bounds.withTop(startY).withBottom(endY));
        graphics.setColour(ana::ui::accent);
        graphics.fillRect(bounds.getX(), startY - 1.0f, bounds.getWidth(), 2.0f);
        graphics.fillRect(bounds.getX(), endY - 1.0f, bounds.getWidth(), 2.0f);
    }

    graphics.setColour(ana::ui::grey500);
    graphics.drawRect(frameBounds, 1);
}

void RangeSlider::mouseDown(const juce::MouseEvent& event)
{
    constexpr float handleHitRadius = 8.0f;
    const auto length = static_cast<float>(std::max(1, orientation == Orientation::horizontal
        ? getWidth() - 1 : getHeight() - 1));
    const auto start = rangeStart * length;
    const auto end = rangeEnd * length;
    const auto mouse = static_cast<float>(orientation == Orientation::horizontal ? event.x : event.y);

    dragStartRangeStart = rangeStart;
    dragStartRangeEnd = rangeEnd;

    if (std::abs(mouse - start) <= handleHitRadius)
        dragMode = DragMode::start;
    else if (std::abs(mouse - end) <= handleHitRadius)
        dragMode = DragMode::end;
    else if (mouse > start && mouse < end)
        dragMode = DragMode::range;
    else
        dragMode = DragMode::none;
}

void RangeSlider::mouseDrag(const juce::MouseEvent& event)
{
    constexpr float minimumRange = 0.01f;
    const auto delta = static_cast<float>(orientation == Orientation::horizontal
        ? event.getDistanceFromDragStartX() : event.getDistanceFromDragStartY())
        / static_cast<float>(std::max(1, orientation == Orientation::horizontal
            ? getWidth() - 1 : getHeight() - 1));

    switch (dragMode)
    {
        case DragMode::start:
            updateRange(juce::jlimit(0.0f, dragStartRangeEnd - minimumRange,
                                     dragStartRangeStart + delta),
                        dragStartRangeEnd);
            break;
        case DragMode::end:
            updateRange(dragStartRangeStart,
                        juce::jlimit(dragStartRangeStart + minimumRange, 1.0f,
                                     dragStartRangeEnd + delta));
            break;
        case DragMode::range:
        {
            const auto rangeWidth = dragStartRangeEnd - dragStartRangeStart;
            const auto newStart = juce::jlimit(0.0f, 1.0f - rangeWidth,
                                               dragStartRangeStart + delta);
            updateRange(newStart, newStart + rangeWidth);
            break;
        }
        case DragMode::none:
            break;
    }
}

void RangeSlider::mouseUp(const juce::MouseEvent&)
{
    dragMode = DragMode::none;
}

void RangeSlider::setRange(const float newStart, const float newEnd)
{
    updateRange(juce::jlimit(0.0f, 1.0f, newStart),
                juce::jlimit(0.0f, 1.0f, newEnd));
}

void RangeSlider::updateRange(const float newStart, const float newEnd)
{
    if (std::abs(rangeStart - newStart) <= 1.0e-6f
        && std::abs(rangeEnd - newEnd) <= 1.0e-6f)
        return;

    rangeStart = newStart;
    rangeEnd = newEnd;
    repaint();

    if (onRangeChanged)
        onRangeChanged();
}

SpectrumView::SpectrumView(PluginProcessor& processorRef)
    : processor(processorRef),
      frequencyLowControl(processorRef.getParameters(), PluginProcessor::frequencyLowParameterId,
                          "FREQ-LOW", [] (const double value) { return formatReadoutFrequency(value); }),
      frequencyHighControl(processorRef.getParameters(), PluginProcessor::frequencyHighParameterId,
                           "FREQ-HIGH", [] (const double value) { return formatReadoutFrequency(value); }),
      rangeLowControl(processorRef.getParameters(), PluginProcessor::frequencyRangeLowParameterId,
                      "RANGE-LOW", [] (const double value) { return formatReadoutLevel(value); }),
      rangeHighControl(processorRef.getParameters(), PluginProcessor::frequencyRangeHighParameterId,
                       "RANGE-HIGH", [] (const double value) { return formatReadoutLevel(value); })
{
    for (auto* component : std::array<juce::Component*, 6> {
             &frequencyLowControl, &frequencyHighControl, &rangeLowControl, &rangeHighControl,
             &frequencyRangeSlider, &magnitudeRangeSlider })
        addAndMakeVisible(*component);

    cursorReadoutLabel.setFont(ana::ui::makeFont());
    cursorReadoutLabel.setJustificationType(juce::Justification::centred);
    cursorReadoutLabel.setText("---", juce::dontSendNotification);
    cursorReadoutLabel.setColour(juce::Label::textColourId, ana::ui::white);
    cursorReadoutLabel.setColour(juce::Label::backgroundColourId, ana::ui::field);
    cursorReadoutLabel.setColour(juce::Label::outlineColourId, ana::ui::grey500);
    cursorReadoutLabel.setBorderSize(juce::BorderSize<int>(1, ana::ui::gap.pixels(), 1, ana::ui::gap.pixels()));
    cursorReadoutLabel.setTextVerticalOffset(readoutTextVerticalOffset);
    cursorReadoutLabel.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(cursorReadoutLabel);

    cursorNoteReadoutLabel.setFont(ana::ui::makeFont());
    cursorNoteReadoutLabel.setJustificationType(juce::Justification::centred);
    cursorNoteReadoutLabel.setText("---", juce::dontSendNotification);
    cursorNoteReadoutLabel.setColour(juce::Label::textColourId, ana::ui::white);
    cursorNoteReadoutLabel.setColour(juce::Label::backgroundColourId, ana::ui::field);
    cursorNoteReadoutLabel.setColour(juce::Label::outlineColourId, ana::ui::grey500);
    cursorNoteReadoutLabel.setBorderSize(juce::BorderSize<int>(1, ana::ui::gap.pixels(), 1, ana::ui::gap.pixels()));
    cursorNoteReadoutLabel.setTextVerticalOffset(readoutTextVerticalOffset);
    cursorNoteReadoutLabel.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(cursorNoteReadoutLabel);

    cursorVerticalReadoutLabel.setFont(ana::ui::makeFont());
    cursorVerticalReadoutLabel.setJustificationType(juce::Justification::centred);
    cursorVerticalReadoutLabel.setText("---", juce::dontSendNotification);
    cursorVerticalReadoutLabel.setColour(juce::Label::textColourId, ana::ui::white);
    cursorVerticalReadoutLabel.setColour(juce::Label::backgroundColourId, ana::ui::field);
    cursorVerticalReadoutLabel.setColour(juce::Label::outlineColourId, ana::ui::grey500);
    cursorVerticalReadoutLabel.setBorderSize(juce::BorderSize<int>(1, ana::ui::gap.pixels(), 1, ana::ui::gap.pixels()));
    cursorVerticalReadoutLabel.setTextVerticalOffset(readoutTextVerticalOffset);
    cursorVerticalReadoutLabel.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(cursorVerticalReadoutLabel);

    static constexpr std::array<const char*, 7> monitorNames { "ST", "LR", "L", "R", "MS", "M", "S" };
    for (size_t index = 0; index < monitorButtons.size(); ++index)
    {
        auto button = std::make_unique<ControlButton>(monitorNames[index]);
        button->onClick = [this, index]
        {
            if (auto* parameter = processor.getParameters().getParameter(
                    PluginProcessor::frequencyChannelModeParameterId))
                parameter->setValueNotifyingHost(parameter->convertTo0to1(static_cast<float>(index)));
        };
        addAndMakeVisible(*button);
        monitorButtons[index] = std::move(button);
    }
    splitButton.setClickingTogglesState(true);
    splitButton.onClick = [this]
    {
        if (auto* parameter = processor.getParameters().getParameter(PluginProcessor::frequencySplitViewParameterId))
            parameter->setValueNotifyingHost(splitButton.getToggleState() ? 1.0f : 0.0f);
    };
    addAndMakeVisible(splitButton);

    for (auto* control : std::array<ParameterControl*, 4> {
             &frequencyLowControl, &frequencyHighControl, &rangeLowControl, &rangeHighControl })
    {
        control->setCompact(true);
        control->onValueChanged = [this] { repaint(); };
    }

    frequencyRangeSlider.onRangeChanged = [this]
    {
        if (! synchronisingRanges)
            updateFrequencyRangeFromSlider();
    };
    magnitudeRangeSlider.onRangeChanged = [this]
    {
        if (! synchronisingRanges)
            updateMagnitudeRangeFromSlider();
    };
    startTimerHz(30);
}

void SpectrumView::paint(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colours::black);
    const auto plotBounds = getPlotBounds();
    if (plotBounds.isEmpty())
        return;

    int fftSize = 0;
    auto sampleRate = processor.getFrequencySpectrum().getSampleRate();
    const auto spectrumType = [this] (const char* parameterId)
    {
        const auto* value = processor.getParameters().getRawParameterValue(parameterId);
        return value != nullptr && value->load(std::memory_order_relaxed) >= 0.5f
            ? ana::freq::SpectrumProcessor::DisplayType::maximum
            : ana::freq::SpectrumProcessor::DisplayType::realtimeAverage;
    };

    const auto* monitorMode = processor.getParameters().getRawParameterValue(
        PluginProcessor::frequencyChannelModeParameterId);
    const auto mode = juce::jlimit(0, 6, monitorMode != nullptr
                                        ? juce::roundToInt(monitorMode->load(std::memory_order_relaxed))
                                        : 0);
    const auto firstChannel = mode == 0 ? ana::freq::SpectrumProcessor::Channel::stereo
                                        : mode == 3 ? ana::freq::SpectrumProcessor::Channel::right
                                        : mode == 5 || mode == 4 ? ana::freq::SpectrumProcessor::Channel::mid
                                        : mode == 6 ? ana::freq::SpectrumProcessor::Channel::side
                                                    : ana::freq::SpectrumProcessor::Channel::left;
    const auto secondChannel = mode == 1 ? ana::freq::SpectrumProcessor::Channel::right
                                         : mode == 4 ? ana::freq::SpectrumProcessor::Channel::side
                                                     : firstChannel;
    const auto* split = processor.getParameters().getRawParameterValue(
        PluginProcessor::frequencySplitViewParameterId);
    const auto supportsSplitView = mode == 1 || mode == 4;
    const auto useSplitView = supportsSplitView && split != nullptr
        && split->load(std::memory_order_relaxed) >= 0.5f;
    const auto* secondSpectrumEnabled = processor.getParameters().getRawParameterValue(
        PluginProcessor::frequencySecondSpectrumParameterId);
    const auto drawSecondSpectrum = useSplitView || secondSpectrumEnabled == nullptr
        || secondSpectrumEnabled->load(std::memory_order_relaxed) >= 0.5f;
    const auto copySpectra = [&] (const ana::freq::SpectrumProcessor& spectrum)
    {
        const auto firstType = spectrumType(PluginProcessor::frequencyFirstSpectrumTypeParameterId);
        spectrum.copySpectrum(firstChannel, firstType,
                              primarySpectrum, fftSize);
        if (drawSecondSpectrum)
            spectrum.copySpectrum(secondChannel,
                                  spectrumType(PluginProcessor::frequencySecondSpectrumTypeParameterId),
                                  secondarySpectrum, fftSize);
    };
    if (processor.isOfflineMode())
    {
        if (const auto snapshot = processor.getOfflineAnalysisSnapshot();
            snapshot != nullptr && snapshot->spectrum != nullptr)
        {
            sampleRate = snapshot->frequencySampleRate;
            copySpectra(*snapshot->spectrum);
        }
    }
    else
    {
        copySpectra(processor.getFrequencySpectrum());
    }

    if (useSplitView)
    {
        auto upperBounds = plotBounds;
        constexpr float dividerHeight = 1.0f;
        upperBounds.setHeight((plotBounds.getHeight() - dividerHeight) * 0.5f);
        auto lowerBounds = upperBounds.withY(upperBounds.getBottom() + dividerHeight);
        graphics.setColour(ana::ui::accent);
        graphics.fillRect(plotBounds.getX(), upperBounds.getBottom(), plotBounds.getWidth(), dividerHeight);
        drawSpectrum(graphics, secondarySpectrum, fftSize, sampleRate, lowerBounds,
                     ana::ui::white, ana::ui::dark);
        drawSpectrum(graphics, primarySpectrum, fftSize, sampleRate, upperBounds,
                     ana::ui::white, ana::ui::light);
    }
    else
    {
        if (drawSecondSpectrum)
            drawSpectrum(graphics, secondarySpectrum, fftSize, sampleRate, plotBounds,
                         ana::ui::white, ana::ui::dark);
        drawSpectrum(graphics, primarySpectrum, fftSize, sampleRate, plotBounds,
                     ana::ui::white, ana::ui::light);
    }

    const auto* cursorReadout = processor.getParameters().getRawParameterValue(
        PluginProcessor::frequencyCursorReadoutParameterId);
    if (cursorInside && (cursorReadout == nullptr || cursorReadout->load(std::memory_order_relaxed) >= 0.5f))
    {
        graphics.setColour(ana::ui::white);
        graphics.drawLine(cursorPosition.x, plotBounds.getY(), cursorPosition.x, plotBounds.getBottom(), 0.5f);
        graphics.drawLine(plotBounds.getX(), cursorPosition.y, plotBounds.getRight(), cursorPosition.y, 0.5f);
    }
}

void SpectrumView::resized()
{
    const auto plotBounds = getPlotBounds().toNearestInt();
    const auto* zoom = processor.getParameters().getRawParameterValue(
        PluginProcessor::frequencyZoomControlsParameterId);
    const auto showZoom = zoom == nullptr || zoom->load(std::memory_order_relaxed) >= 0.5f;
    constexpr int frequencyReadoutWidth = ana::ui::textControlWidth(8);
    constexpr int levelReadoutWidth = ana::ui::textControlWidth(7);
    const auto readoutY = plotBounds.getBottom() - ana::ui::controlHeight;
    const auto graphRight = plotBounds.getRight();
    auto topArea = juce::Rectangle<int>(plotBounds.getX(), plotBounds.getY(),
                                        plotBounds.getWidth(), ana::ui::controlHeight);
    auto topReadouts = topArea.removeFromRight(levelReadoutWidth * 2 + ana::ui::gap.pixels());
    ana::ui::gap.removeFromRight(topArea);
    ana::ui::FixedGapRow topControls(topArea);
    cursorReadoutLabel.setBounds(topControls.takeLeft(frequencyReadoutWidth));
    cursorNoteReadoutLabel.setBounds(topControls.takeLeft(ana::ui::textControlWidth(4)));
    ana::ui::FixedGapRow monitorControls(topControls.remaining());
    for (auto& button : monitorButtons)
        button->setBounds(monitorControls.takeLeft(button->getPreferredWidth()));
    splitButtonFits = monitorControls.remaining().getWidth() >= splitButton.getPreferredWidth();
    splitButton.setBounds(splitButtonFits
        ? monitorControls.takeLeft(splitButton.getPreferredWidth()) : juce::Rectangle<int>());
    ana::ui::FixedGapRow topReadoutControls(topReadouts);
    cursorVerticalReadoutLabel.setBounds(topReadoutControls.takeLeft(levelReadoutWidth));
    rangeHighControl.setBounds(topReadoutControls.takeLeft(levelReadoutWidth));
    frequencyLowControl.setBounds(plotBounds.getX(), readoutY, frequencyReadoutWidth, ana::ui::controlHeight);
    frequencyHighControl.setBounds(graphRight - frequencyReadoutWidth - levelReadoutWidth
                                       - ana::ui::gap.pixels(), readoutY,
                                   frequencyReadoutWidth, ana::ui::controlHeight);
    rangeLowControl.setBounds(graphRight - levelReadoutWidth, readoutY,
                              levelReadoutWidth, ana::ui::controlHeight);
    if (showZoom)
    {
        frequencyRangeSlider.setBounds(0, getHeight() - bandRangeSliderHeight,
                                       getWidth(), bandRangeSliderHeight);
        magnitudeRangeSlider.setBounds(getWidth() - bandZoomSliderWidth, 0,
                                       bandZoomSliderWidth,
                                       getHeight() - bandRangeSliderHeight
                                           - ana::ui::gap.pixels());
    }
    else
    {
        frequencyRangeSlider.setBounds({});
        magnitudeRangeSlider.setBounds({});
    }
    syncRangeSliders();
    refreshMonitorControls();
}

void SpectrumView::mouseDown(const juce::MouseEvent& event)
{
    if (event.originalComponent == this)
        processor.clearFrequencySpectrum();
}

void SpectrumView::mouseMove(const juce::MouseEvent& event)
{
    const auto nextCursorInside = getPlotBounds().contains(event.position);
    if (cursorInside == nextCursorInside && cursorPosition == event.position)
        return;

    cursorInside = nextCursorInside;
    cursorPosition = event.position;
    if (cursorInside)
    {
        const auto readParameter = [this] (const char* parameterId, const float fallback)
        {
            if (const auto* value = processor.getParameters().getRawParameterValue(parameterId))
                return value->load(std::memory_order_relaxed);
            return fallback;
        };
        const auto lowFrequency = std::max(20.0f, readParameter(PluginProcessor::frequencyLowParameterId, 20.0f));
        const auto highFrequency = std::max(lowFrequency + 1.0f,
                                            readParameter(PluginProcessor::frequencyHighParameterId, 20000.0f));
        const auto normalisedX = juce::jlimit(0.0f, 1.0f,
                                              (cursorPosition.x - getPlotBounds().getX())
                                                  / getPlotBounds().getWidth());
        lastCursorFrequency = lowFrequency * std::pow(highFrequency / lowFrequency, normalisedX);
        cursorReadoutLabel.setText(formatReadoutFrequency(lastCursorFrequency), juce::dontSendNotification);
        static constexpr std::array<const char*, 12> noteNames {
            "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
        };
        const auto midiNote = juce::roundToInt(69.0 + 12.0 * std::log2(lastCursorFrequency / 440.0f));
        const auto noteIndex = (midiNote % 12 + 12) % 12;
        cursorNoteReadoutLabel.setText(juce::String(noteNames[static_cast<size_t>(noteIndex)])
                                           + juce::String(midiNote / 12 - 1),
                                       juce::dontSendNotification);
        const auto lowRange = readParameter(PluginProcessor::frequencyRangeLowParameterId, -96.0f);
        const auto highRange = std::max(lowRange + 1.0f,
            readParameter(PluginProcessor::frequencyRangeHighParameterId, 0.0f));
        const auto normalisedY = juce::jlimit(0.0f, 1.0f,
            (cursorPosition.y - getPlotBounds().getY()) / getPlotBounds().getHeight());
        cursorVerticalReadoutLabel.setText(formatReadoutLevel(
                                               highRange - normalisedY * (highRange - lowRange)),
                                           juce::dontSendNotification);
    }
    repaint();
}

void SpectrumView::mouseExit(const juce::MouseEvent&)
{
    if (! cursorInside)
        return;

    cursorInside = false;
    repaint();
}

juce::Rectangle<float> SpectrumView::getPlotBounds() const noexcept
{
    auto bounds = getLocalBounds().toFloat();
    const auto* zoom = processor.getParameters().getRawParameterValue(
        PluginProcessor::frequencyZoomControlsParameterId);
    const auto showZoom = zoom == nullptr || zoom->load(std::memory_order_relaxed) >= 0.5f;
    if (showZoom)
    {
        bounds.removeFromBottom(static_cast<float>(bandRangeSliderHeight
                                                    + ana::ui::gap.pixels()));
        bounds.removeFromRight(static_cast<float>(bandZoomSliderWidth
                                                   + ana::ui::gap.pixels()));
    }
    return bounds;
}

void SpectrumView::timerCallback()
{
    syncRangeSliders();
    refreshMonitorControls();
    if (processor.isOfflineMode())
    {
        if (const auto snapshot = processor.getOfflineAnalysisSnapshot();
            snapshot != nullptr && displayedOfflineRevision != snapshot->revision)
        {
            displayedOfflineRevision = snapshot->revision;
            repaint();
        }
        return;
    }

    const auto currentRevision = processor.getFrequencySpectrum().getRevision();
    if (displayedRevision != currentRevision)
    {
        displayedRevision = currentRevision;
        repaint();
    }
}

void SpectrumView::refreshMonitorControls()
{
    const auto readValue = [this] (const char* parameterId, const float fallback)
    {
        if (const auto* value = processor.getParameters().getRawParameterValue(parameterId))
            return value->load(std::memory_order_relaxed);
        return fallback;
    };
    const auto showMonitor = readValue(PluginProcessor::frequencyMonitorControlsParameterId, 1.0f) >= 0.5f;
    const auto showZoom = readValue(PluginProcessor::frequencyZoomControlsParameterId, 1.0f) >= 0.5f;
    const auto mode = juce::jlimit(0, static_cast<int>(monitorButtons.size()) - 1,
                                   juce::roundToInt(readValue(PluginProcessor::frequencyChannelModeParameterId, 0.0f)));
    const auto splitAvailable = mode == 1 || mode == 4;
    for (size_t index = 0; index < monitorButtons.size(); ++index)
    {
        monitorButtons[index]->setVisible(showMonitor);
        monitorButtons[index]->setToggleState(static_cast<int>(index) == mode, juce::dontSendNotification);
    }
    splitButton.setVisible(showMonitor && splitButtonFits);
    splitButton.setEnabled(splitAvailable);
    splitButton.setToggleState(splitAvailable
                                   && readValue(PluginProcessor::frequencySplitViewParameterId, 0.0f) >= 0.5f,
                               juce::dontSendNotification);
    frequencyRangeSlider.setVisible(showZoom);
    magnitudeRangeSlider.setVisible(showZoom);
}

void SpectrumView::syncRangeSliders()
{
    const auto readParameter = [this] (const char* parameterId, const float fallback)
    {
        if (const auto* value = processor.getParameters().getRawParameterValue(parameterId))
            return value->load(std::memory_order_relaxed);
        return fallback;
    };
    const auto lowFrequency = readParameter(PluginProcessor::frequencyLowParameterId, 20.0f);
    const auto highFrequency = readParameter(PluginProcessor::frequencyHighParameterId, 20000.0f);
    const auto lowRange = readParameter(PluginProcessor::frequencyRangeLowParameterId, -96.0f);
    const auto highRange = readParameter(PluginProcessor::frequencyRangeHighParameterId, 0.0f);
    const auto* rangesVisible = processor.getParameters().getRawParameterValue(
        PluginProcessor::frequencyRangesVisibleParameterId);
    const auto* cursorReadout = processor.getParameters().getRawParameterValue(
        PluginProcessor::frequencyCursorReadoutParameterId);
    const auto shouldShowRanges = rangesVisible == nullptr || rangesVisible->load(std::memory_order_relaxed) >= 0.5f;

    for (auto* control : std::array<ParameterControl*, 4> {
             &frequencyLowControl, &frequencyHighControl, &rangeLowControl, &rangeHighControl })
        control->setVisible(shouldShowRanges);
    cursorReadoutLabel.setVisible(cursorReadout == nullptr || cursorReadout->load(std::memory_order_relaxed) >= 0.5f);
    cursorNoteReadoutLabel.setVisible(cursorReadout == nullptr || cursorReadout->load(std::memory_order_relaxed) >= 0.5f);
    cursorVerticalReadoutLabel.setVisible(cursorReadout == nullptr || cursorReadout->load(std::memory_order_relaxed) >= 0.5f);

    const juce::ScopedValueSetter<bool> guard(synchronisingRanges, true);
    frequencyRangeSlider.setRange(frequencyToNormalised(std::min(lowFrequency, highFrequency)),
                                  frequencyToNormalised(std::max(lowFrequency, highFrequency)));
    magnitudeRangeSlider.setRange((24.0f - std::max(lowRange, highRange)) / 144.0f,
                                  (24.0f - std::min(lowRange, highRange)) / 144.0f);
}

void SpectrumView::updateFrequencyRangeFromSlider()
{
    const auto lowFrequency = normalisedToFrequency(frequencyRangeSlider.getRangeStart());
    const auto highFrequency = normalisedToFrequency(frequencyRangeSlider.getRangeEnd());
    frequencyLowControl.getSlider().setValue(lowFrequency, juce::sendNotificationSync);
    frequencyHighControl.getSlider().setValue(highFrequency, juce::sendNotificationSync);
    repaint();
}

void SpectrumView::updateMagnitudeRangeFromSlider()
{
    const auto highRange = 24.0f - magnitudeRangeSlider.getRangeStart() * 144.0f;
    const auto lowRange = 24.0f - magnitudeRangeSlider.getRangeEnd() * 144.0f;
    rangeLowControl.getSlider().setValue(lowRange, juce::sendNotificationSync);
    rangeHighControl.getSlider().setValue(highRange, juce::sendNotificationSync);
    repaint();
}

void SpectrumView::drawSpectrum(juce::Graphics& graphics,
                                                 const std::vector<float>& spectrum,
                                                 const int fftSize,
                                                 const double sampleRate,
                                                 const juce::Rectangle<float> plotBounds,
                                                 const juce::Colour lineColour,
                                                 const juce::Colour fillColour) const
{
    if (fftSize <= 0 || spectrum.empty())
        return;

    const auto readParameter = [this] (const char* parameterId, const float fallback)
    {
        if (const auto* value = processor.getParameters().getRawParameterValue(parameterId))
            return value->load(std::memory_order_relaxed);
        return fallback;
    };
    const auto lowFrequency = std::max(20.0f, readParameter(PluginProcessor::frequencyLowParameterId, 20.0f));
    const auto highFrequency = std::max(lowFrequency + 1.0f,
                                        readParameter(PluginProcessor::frequencyHighParameterId, 20000.0f));
    const auto lowRange = readParameter(PluginProcessor::frequencyRangeLowParameterId, -96.0f);
    const auto highRange = std::max(lowRange + 1.0f,
                                    readParameter(PluginProcessor::frequencyRangeHighParameterId, 0.0f));
    const auto slope = readParameter(PluginProcessor::frequencySlopeParameterId, 0.0f);
    const auto binFrequency = static_cast<float>(sampleRate) / static_cast<float>(fftSize);
    const auto lowLog = std::log(lowFrequency);
    const auto highLog = std::log(highFrequency);
    const auto logSpan = std::max(0.0001f, highLog - lowLog);

    bool hasAudibleData = false;
    const auto columnCount = std::max(1, static_cast<int>(std::ceil(plotBounds.getWidth())));
    std::vector<float> columnPowers(static_cast<size_t>(columnCount), 0.0f);
    std::vector<int> columnCounts(static_cast<size_t>(columnCount), 0);
    for (size_t bin = 1; bin < spectrum.size(); ++bin)
    {
        const auto frequency = static_cast<float>(bin) * binFrequency;
        if (frequency < lowFrequency || frequency > highFrequency)
            continue;

        const auto normalisedFrequency = (std::log(frequency) - lowLog) / logSpan;
        const auto slopedValue = spectrum[bin] + slope * normalisedFrequency;
        hasAudibleData = hasAudibleData || slopedValue > lowRange + 0.01f;
        const auto column = juce::jlimit(0, columnCount - 1,
            static_cast<int>(std::floor(normalisedFrequency * plotBounds.getWidth())));
        const auto gain = juce::Decibels::decibelsToGain(slopedValue);
        columnPowers[static_cast<size_t>(column)] += gain * gain;
        ++columnCounts[static_cast<size_t>(column)];
    }

    const auto smoothing = readParameter(PluginProcessor::frequencySmoothingParameterId, 30.0f);
    const auto smoothingRadius = juce::jlimit(0, 48, juce::roundToInt(smoothing * 0.48f));
    const auto sigma = std::max(0.5f, static_cast<float>(smoothingRadius) * 0.5f);
    juce::Path path;
    juce::Point<float> firstPoint;
    juce::Point<float> lastPoint;
    bool hasPoint = false;
    for (int column = 0; column < columnCount; ++column)
    {
        auto summedPower = 0.0f;
        auto summedWeight = 0.0f;
        const auto firstColumn = std::max(0, column - smoothingRadius);
        const auto lastColumn = std::min(columnCount - 1, column + smoothingRadius);
        for (int neighbour = firstColumn; neighbour <= lastColumn; ++neighbour)
        {
            const auto count = columnCounts[static_cast<size_t>(neighbour)];
            if (count == 0)
                continue;

            const auto distance = static_cast<float>(std::abs(neighbour - column));
            const auto weight = std::exp(-0.5f * distance * distance / (sigma * sigma));
            summedPower += columnPowers[static_cast<size_t>(neighbour)] * weight;
            summedWeight += static_cast<float>(count) * weight;
        }
        if (summedWeight <= 0.0f)
            continue;

        const auto averagedDecibels = juce::Decibels::gainToDecibels(
            std::sqrt(summedPower / summedWeight), lowRange);
        const auto normalisedLevel = juce::jlimit(0.0f, 1.0f,
            (averagedDecibels - lowRange) / (highRange - lowRange));
        const auto point = juce::Point<float>(plotBounds.getX() + static_cast<float>(column) + 0.5f,
                                              plotBounds.getBottom() - normalisedLevel * plotBounds.getHeight());
        if (! hasPoint)
        {
            firstPoint = point.withX(plotBounds.getX());
            path.startNewSubPath(firstPoint);
            hasPoint = true;
        }
        else
        {
            path.lineTo(point);
        }
        lastPoint = point;
    }

    if (! hasPoint || ! hasAudibleData)
        return;

    const auto* filled = processor.getParameters().getRawParameterValue(
        PluginProcessor::frequencyFilledDisplayParameterId);
    if (filled != nullptr && filled->load(std::memory_order_relaxed) >= 0.5f)
    {
        auto fillPath = path;
        fillPath.lineTo(lastPoint.x, plotBounds.getBottom());
        fillPath.lineTo(firstPoint.x, plotBounds.getBottom());
        fillPath.closeSubPath();
        graphics.setColour(fillColour);
        graphics.fillPath(fillPath);
    }

    graphics.setColour(lineColour);
    const auto* antiAlias = processor.getParameters().getRawParameterValue(
        PluginProcessor::frequencyAntiAliasParameterId);
    if (antiAlias == nullptr || antiAlias->load(std::memory_order_relaxed) >= 0.5f)
    {
        graphics.strokePath(path, juce::PathStrokeType(1.0f));
    }
    else
    {
        juce::PathFlatteningIterator iterator(path, juce::AffineTransform(), 0.25f);
        while (iterator.next())
            graphics.drawLine(static_cast<float>(juce::roundToInt(iterator.x1)),
                              static_cast<float>(juce::roundToInt(iterator.y1)),
                              static_cast<float>(juce::roundToInt(iterator.x2)),
                              static_cast<float>(juce::roundToInt(iterator.y2)), 1.0f);
    }
}

float SpectrumView::frequencyToNormalised(const float frequency) noexcept
{
    return juce::jlimit(0.0f, 1.0f, std::log(std::max(20.0f, frequency) / 20.0f) / std::log(1000.0f));
}

float SpectrumView::normalisedToFrequency(const float normalised) noexcept
{
    return 20.0f * std::pow(1000.0f, juce::jlimit(0.0f, 1.0f, normalised));
}

CorrelationView::CorrelationView(PluginProcessor& processorRef)
    : processor(processorRef),
      frequencyLowControl(processorRef.getParameters(), PluginProcessor::correlationLowParameterId,
                          "LOW", [] (const double value) { return formatReadoutFrequency(value); }),
      frequencyHighControl(processorRef.getParameters(), PluginProcessor::correlationHighParameterId,
                           "HIGH", [] (const double value) { return formatReadoutFrequency(value); }),
      rangeLowControl(processorRef.getParameters(), PluginProcessor::correlationRangeLowParameterId,
                      "LOW", [] (const double value) { return formatCorrelationCoefficient(value); }),
      rangeHighControl(processorRef.getParameters(), PluginProcessor::correlationRangeHighParameterId,
                       "HIGH", [] (const double value) { return formatCorrelationCoefficient(value); })
{
    for (auto* component : std::array<juce::Component*, 9> {
             &frequencyLowControl, &frequencyHighControl, &rangeLowControl, &rangeHighControl,
             &cursorReadoutLabel, &cursorVerticalReadoutLabel, &phaseModeButton, &amplitudeModeButton,
             &frequencyRangeSlider })
        addAndMakeVisible(*component);
    addAndMakeVisible(correlationRangeSlider);

    for (auto* control : std::array<ParameterControl*, 4> {
             &frequencyLowControl, &frequencyHighControl, &rangeLowControl, &rangeHighControl })
    {
        control->setCompact(true);
        control->onValueChanged = [this] { repaint(); };
    }
    cursorReadoutLabel.setFont(ana::ui::makeFont());
    cursorReadoutLabel.setJustificationType(juce::Justification::centred);
    cursorReadoutLabel.setText("---", juce::dontSendNotification);
    cursorReadoutLabel.setColour(juce::Label::textColourId, ana::ui::white);
    cursorReadoutLabel.setColour(juce::Label::backgroundColourId, ana::ui::field);
    cursorReadoutLabel.setColour(juce::Label::outlineColourId, ana::ui::grey500);
    cursorReadoutLabel.setBorderSize(juce::BorderSize<int>(1, ana::ui::gap.pixels(), 1, ana::ui::gap.pixels()));
    cursorReadoutLabel.setTextVerticalOffset(readoutTextVerticalOffset);
    cursorReadoutLabel.setInterceptsMouseClicks(false, false);

    cursorVerticalReadoutLabel.setFont(ana::ui::makeFont());
    cursorVerticalReadoutLabel.setJustificationType(juce::Justification::centred);
    cursorVerticalReadoutLabel.setText("---", juce::dontSendNotification);
    cursorVerticalReadoutLabel.setColour(juce::Label::textColourId, ana::ui::white);
    cursorVerticalReadoutLabel.setColour(juce::Label::backgroundColourId, ana::ui::field);
    cursorVerticalReadoutLabel.setColour(juce::Label::outlineColourId, ana::ui::grey500);
    cursorVerticalReadoutLabel.setBorderSize(juce::BorderSize<int>(1, ana::ui::gap.pixels(), 1, ana::ui::gap.pixels()));
    cursorVerticalReadoutLabel.setTextVerticalOffset(readoutTextVerticalOffset);
    cursorVerticalReadoutLabel.setInterceptsMouseClicks(false, false);

    phaseModeButton.onClick = [this] { setCorrelationMode(0); };
    amplitudeModeButton.onClick = [this] { setCorrelationMode(1); };
    frequencyRangeSlider.onRangeChanged = [this]
    {
        if (! synchronisingRanges)
            updateFrequencyRangeFromSlider();
    };
    correlationRangeSlider.onRangeChanged = [this]
    {
        if (! synchronisingRanges)
            updateCorrelationRangeFromSlider();
    };
    startTimerHz(30);
}

void CorrelationView::paint(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colours::black);
    const auto plotBounds = getPlotBounds();
    if (plotBounds.isEmpty())
        return;

    int fftSize = 0;
    const auto* modeParameter = processor.getParameters().getRawParameterValue(
        PluginProcessor::correlationModeParameterId);
    const auto mode = modeParameter != nullptr && modeParameter->load(std::memory_order_relaxed) >= 0.5f
        ? ana::corr::StereoProcessor::Mode::amplitude
        : ana::corr::StereoProcessor::Mode::phase;
    const auto displayType = [this] (const char* parameterId)
    {
        const auto* value = processor.getParameters().getRawParameterValue(parameterId);
        return value != nullptr && value->load(std::memory_order_relaxed) >= 0.5f
            ? ana::corr::StereoProcessor::DisplayType::maximum
            : ana::corr::StereoProcessor::DisplayType::realtimeAverage;
    };
    const auto* displayedSpectrum = &processor.getCorrelationSpectrum();
    auto sampleRate = displayedSpectrum->getSampleRate();
    if (processor.isOfflineMode())
    {
        if (const auto snapshot = processor.getOfflineAnalysisSnapshot();
            snapshot != nullptr && snapshot->correlation != nullptr)
        {
            displayedSpectrum = snapshot->correlation.get();
            sampleRate = snapshot->correlationSampleRate;
        }
    }

    displayedSpectrum->copyCorrelation(
        mode, displayType(PluginProcessor::correlationFirstSpectrumTypeParameterId),
        primaryCorrelation, fftSize);
    if (fftSize <= 0 || primaryCorrelation.empty())
        return;

    const auto readParameter = [this] (const char* parameterId, const float fallback)
    {
        if (const auto* value = processor.getParameters().getRawParameterValue(parameterId))
            return value->load(std::memory_order_relaxed);
        return fallback;
    };
    const auto lowFrequency = std::max(20.0f, readParameter(PluginProcessor::correlationLowParameterId, 20.0f));
    const auto highFrequency = std::max(lowFrequency + 1.0f,
                                        readParameter(PluginProcessor::correlationHighParameterId, 20000.0f));
    const auto lowRange = readParameter(PluginProcessor::correlationRangeLowParameterId, -1.0f);
    const auto highRange = std::max(lowRange + 0.01f,
                                    readParameter(PluginProcessor::correlationRangeHighParameterId, 1.0f));
    const auto zeroY = plotBounds.getBottom() - juce::jlimit(0.0f, 1.0f,
        (0.0f - lowRange) / (highRange - lowRange)) * plotBounds.getHeight();
    drawCorrelation(graphics, primaryCorrelation, plotBounds, lowFrequency, highFrequency,
                    lowRange, highRange, sampleRate, fftSize,
                    ana::ui::white, ana::ui::light);

    const auto* secondSpectrum = processor.getParameters().getRawParameterValue(
        PluginProcessor::correlationSecondSpectrumParameterId);
    if (secondSpectrum != nullptr && secondSpectrum->load(std::memory_order_relaxed) >= 0.5f)
    {
        int secondaryFftSize = 0;
        displayedSpectrum->copyCorrelation(
            mode, displayType(PluginProcessor::correlationSecondSpectrumTypeParameterId),
            secondaryCorrelation, secondaryFftSize);
        if (secondaryFftSize == fftSize)
            drawCorrelation(graphics, secondaryCorrelation, plotBounds, lowFrequency, highFrequency,
                            lowRange, highRange, sampleRate, fftSize,
                            ana::ui::white, ana::ui::dark);
    }

    graphics.setColour(ana::ui::grey500);
    graphics.fillRect(plotBounds.getX(), zeroY, plotBounds.getWidth(), 1.0f);

    const auto* cursorReadout = processor.getParameters().getRawParameterValue(
        PluginProcessor::correlationCursorReadoutParameterId);
    if (cursorInside && (cursorReadout == nullptr || cursorReadout->load(std::memory_order_relaxed) >= 0.5f))
    {
        graphics.setColour(ana::ui::white);
        graphics.drawLine(cursorPosition.x, plotBounds.getY(), cursorPosition.x, plotBounds.getBottom(), 0.5f);
        graphics.drawLine(plotBounds.getX(), cursorPosition.y, plotBounds.getRight(), cursorPosition.y, 0.5f);
    }
}

void CorrelationView::drawCorrelation(juce::Graphics& graphics,
                                                      const std::vector<float>& values,
                                                      const juce::Rectangle<float> plotBounds,
                                                      const float lowFrequency,
                                                      const float highFrequency,
                                                      const float lowRange,
                                                      const float highRange,
                                                      const double sampleRate,
                                                      const int fftSize,
                                                      const juce::Colour lineColour,
                                                      const juce::Colour fillColour)
{
    if (fftSize <= 0 || values.empty())
        return;

    const auto binFrequency = static_cast<float>(sampleRate) / static_cast<float>(fftSize);
    const auto lowLog = std::log(lowFrequency);
    const auto logSpan = std::max(0.0001f, std::log(highFrequency) - lowLog);
    const auto columnCount = std::max(1, static_cast<int>(std::ceil(plotBounds.getWidth())));
    correlationColumns.assign(static_cast<size_t>(columnCount), 0.0f);
    correlationColumnCounts.assign(static_cast<size_t>(columnCount), 0);
    for (size_t bin = 1; bin < values.size(); ++bin)
    {
        const auto frequency = static_cast<float>(bin) * binFrequency;
        if (frequency < lowFrequency || frequency > highFrequency)
            continue;
        const auto normalisedFrequency = (std::log(frequency) - lowLog) / logSpan;
        const auto column = juce::jlimit(0, columnCount - 1,
            static_cast<int>(std::floor(normalisedFrequency * plotBounds.getWidth())));
        correlationColumns[static_cast<size_t>(column)] += values[bin];
        ++correlationColumnCounts[static_cast<size_t>(column)];
    }

    const auto* smoothingParameter = processor.getParameters().getRawParameterValue(
        PluginProcessor::correlationSmoothingParameterId);
    const auto smoothing = smoothingParameter != nullptr
        ? smoothingParameter->load(std::memory_order_relaxed) : 30.0f;
    const auto smoothingRadius = juce::jlimit(0, 48, juce::roundToInt(smoothing * 0.48f));
    juce::Path path;
    juce::Point<float> firstPoint;
    juce::Point<float> lastPoint;
    auto hasPoint = false;
    for (int column = 0; column < columnCount; ++column)
    {
        auto summedCorrelation = 0.0f;
        auto summedWeight = 0.0f;
        const auto firstColumn = std::max(0, column - smoothingRadius);
        const auto lastColumn = std::min(columnCount - 1, column + smoothingRadius);
        for (int neighbour = firstColumn; neighbour <= lastColumn; ++neighbour)
        {
            const auto count = correlationColumnCounts[static_cast<size_t>(neighbour)];
            if (count == 0)
                continue;

            const auto distance = static_cast<float>(std::abs(neighbour - column));
            const auto sigma = std::max(0.5f, static_cast<float>(smoothingRadius) * 0.5f);
            const auto weight = std::exp(-0.5f * distance * distance / (sigma * sigma));
            summedCorrelation += correlationColumns[static_cast<size_t>(neighbour)] * weight;
            summedWeight += static_cast<float>(count) * weight;
        }
        if (summedWeight <= 0.0f)
            continue;

        const auto value = summedCorrelation / summedWeight;
        const auto y = plotBounds.getBottom() - juce::jlimit(0.0f, 1.0f,
            (value - lowRange) / (highRange - lowRange)) * plotBounds.getHeight();
        const auto point = juce::Point<float>(plotBounds.getX() + static_cast<float>(column) + 0.5f, y);
        if (! hasPoint)
        {
            firstPoint = point.withX(plotBounds.getX());
            path.startNewSubPath(firstPoint);
            hasPoint = true;
        }
        else
        {
            path.lineTo(point);
        }
        lastPoint = point;
    }

    if (! hasPoint)
        return;

    const auto* filled = processor.getParameters().getRawParameterValue(
        PluginProcessor::correlationFilledDisplayParameterId);
    if (filled == nullptr || filled->load(std::memory_order_relaxed) >= 0.5f)
    {
        auto fillPath = path;
        fillPath.lineTo(lastPoint.x, plotBounds.getBottom());
        fillPath.lineTo(firstPoint.x, plotBounds.getBottom());
        fillPath.closeSubPath();
        graphics.setColour(fillColour);
        graphics.fillPath(fillPath);
    }

    graphics.setColour(lineColour);
    graphics.strokePath(path, juce::PathStrokeType(1.0f));
}

void CorrelationView::resized()
{
    const auto plotBounds = getPlotBounds().toNearestInt();
    const auto* zoom = processor.getParameters().getRawParameterValue(
        PluginProcessor::correlationZoomControlsParameterId);
    const auto showZoom = zoom == nullptr || zoom->load(std::memory_order_relaxed) >= 0.5f;
    constexpr int frequencyReadoutWidth = ana::ui::textControlWidth(8);
    constexpr int coefficientReadoutWidth = ana::ui::textControlWidth(5);
    const auto readoutY = plotBounds.getBottom() - ana::ui::controlHeight;
    const auto graphRight = plotBounds.getRight();
    auto topArea = juce::Rectangle<int>(plotBounds.getX(), plotBounds.getY(),
                                        plotBounds.getWidth(), ana::ui::controlHeight);
    auto topReadouts = topArea.removeFromRight(coefficientReadoutWidth * 2
                                              + ana::ui::gap.pixels());
    ana::ui::gap.removeFromRight(topArea);
    ana::ui::FixedGapRow topControls(topArea);
    cursorReadoutLabel.setBounds(topControls.takeLeft(frequencyReadoutWidth));
    phaseModeButton.setBounds(topControls.takeLeft(phaseModeButton.getPreferredWidth()));
    amplitudeModeButton.setBounds(topControls.takeLeft(amplitudeModeButton.getPreferredWidth()));
    ana::ui::FixedGapRow topReadoutControls(topReadouts);
    cursorVerticalReadoutLabel.setBounds(topReadoutControls.takeLeft(coefficientReadoutWidth));
    rangeHighControl.setBounds(topReadoutControls.takeLeft(coefficientReadoutWidth));
    frequencyLowControl.setBounds(plotBounds.getX(), readoutY, frequencyReadoutWidth, ana::ui::controlHeight);
    frequencyHighControl.setBounds(graphRight - frequencyReadoutWidth - coefficientReadoutWidth
                                       - ana::ui::gap.pixels(), readoutY,
                                   frequencyReadoutWidth, ana::ui::controlHeight);
    rangeLowControl.setBounds(graphRight - coefficientReadoutWidth, readoutY,
                              coefficientReadoutWidth, ana::ui::controlHeight);
    if (showZoom)
    {
        frequencyRangeSlider.setBounds(0, getHeight() - bandRangeSliderHeight,
                                       getWidth(), bandRangeSliderHeight);
        correlationRangeSlider.setBounds(getWidth() - bandRangeSliderHeight, 0, bandRangeSliderHeight,
                                         getHeight() - bandRangeSliderHeight - ana::ui::gap.pixels());
    }
    else
    {
        frequencyRangeSlider.setBounds({});
        correlationRangeSlider.setBounds({});
    }
    syncRangeSliders();
    refreshControls();
}

void CorrelationView::mouseDown(const juce::MouseEvent& event)
{
    if (event.originalComponent == this)
        processor.clearCorrelationSpectrum();
}

void CorrelationView::mouseMove(const juce::MouseEvent& event)
{
    const auto plotBounds = getPlotBounds();
    const auto nextCursorInside = plotBounds.contains(event.position);
    if (cursorInside == nextCursorInside && cursorPosition == event.position)
        return;
    cursorInside = nextCursorInside;
    cursorPosition = event.position;
    if (cursorInside)
    {
        const auto* low = processor.getParameters().getRawParameterValue(PluginProcessor::correlationLowParameterId);
        const auto* high = processor.getParameters().getRawParameterValue(PluginProcessor::correlationHighParameterId);
        const auto lowFrequency = std::max(20.0f, low != nullptr ? low->load(std::memory_order_relaxed) : 20.0f);
        const auto highFrequency = std::max(lowFrequency + 1.0f,
            high != nullptr ? high->load(std::memory_order_relaxed) : 20000.0f);
        const auto normalisedX = juce::jlimit(0.0f, 1.0f,
            (cursorPosition.x - plotBounds.getX()) / plotBounds.getWidth());
        lastCursorFrequency = lowFrequency * std::pow(highFrequency / lowFrequency, normalisedX);
        cursorReadoutLabel.setText(formatReadoutFrequency(lastCursorFrequency), juce::dontSendNotification);
        const auto* lowRangeParameter = processor.getParameters().getRawParameterValue(
            PluginProcessor::correlationRangeLowParameterId);
        const auto* highRangeParameter = processor.getParameters().getRawParameterValue(
            PluginProcessor::correlationRangeHighParameterId);
        const auto lowRange = lowRangeParameter != nullptr
            ? lowRangeParameter->load(std::memory_order_relaxed) : -1.0f;
        const auto highRange = std::max(lowRange + 0.01f,
            highRangeParameter != nullptr ? highRangeParameter->load(std::memory_order_relaxed) : 1.0f);
        const auto normalisedY = juce::jlimit(0.0f, 1.0f,
            (cursorPosition.y - plotBounds.getY()) / plotBounds.getHeight());
        cursorVerticalReadoutLabel.setText(formatCorrelationCoefficient(
                                               highRange - normalisedY * (highRange - lowRange)),
                                           juce::dontSendNotification);
    }
    repaint();
}

void CorrelationView::mouseExit(const juce::MouseEvent&)
{
    cursorInside = false;
    repaint();
}

void CorrelationView::timerCallback()
{
    syncRangeSliders();
    refreshControls();
    if (processor.isOfflineMode())
    {
        if (offlineRenderPending)
        {
            offlineRenderPending = false;
            repaint();
            if (onOfflineUpdateStatus)
                onOfflineUpdateStatus("UPDATED");
        }
        if (const auto snapshot = processor.getOfflineAnalysisSnapshot();
            snapshot != nullptr && displayedOfflineRevision != snapshot->revision)
        {
            displayedOfflineRevision = snapshot->revision;
            repaint();
        }
        return;
    }

    const auto revision = processor.getCorrelationSpectrum().getRevision();
    if (displayedRevision != revision)
    {
        displayedRevision = revision;
        repaint();
    }
}

void CorrelationView::setCorrelationMode(const int mode)
{
    if (auto* parameter = processor.getParameters().getParameter(PluginProcessor::correlationModeParameterId))
        parameter->setValueNotifyingHost(parameter->convertTo0to1(static_cast<float>(mode)));

    if (! processor.isOfflineMode())
        return;

    offlineRenderPending = true;
    if (onOfflineUpdateStatus)
        onOfflineUpdateStatus("UPDATING 00");
}

void CorrelationView::syncRangeSliders()
{
    const auto read = [this] (const char* id, const float fallback)
    {
        if (const auto* value = processor.getParameters().getRawParameterValue(id))
            return value->load(std::memory_order_relaxed);
        return fallback;
    };
    const auto lowFrequency = read(PluginProcessor::correlationLowParameterId, 20.0f);
    const auto highFrequency = read(PluginProcessor::correlationHighParameterId, 20000.0f);
    const auto lowRange = read(PluginProcessor::correlationRangeLowParameterId, -1.0f);
    const auto highRange = read(PluginProcessor::correlationRangeHighParameterId, 1.0f);
    const auto* ranges = processor.getParameters().getRawParameterValue(PluginProcessor::correlationRangesVisibleParameterId);
    const auto* cursor = processor.getParameters().getRawParameterValue(PluginProcessor::correlationCursorReadoutParameterId);
    const auto showRanges = ranges == nullptr || ranges->load(std::memory_order_relaxed) >= 0.5f;
    for (auto* control : std::array<ParameterControl*, 4> {
             &frequencyLowControl, &frequencyHighControl, &rangeLowControl, &rangeHighControl })
        control->setVisible(showRanges);
    cursorReadoutLabel.setVisible(cursor == nullptr || cursor->load(std::memory_order_relaxed) >= 0.5f);
    cursorVerticalReadoutLabel.setVisible(cursor == nullptr || cursor->load(std::memory_order_relaxed) >= 0.5f);
    const juce::ScopedValueSetter<bool> guard(synchronisingRanges, true);
    frequencyRangeSlider.setRange(frequencyToNormalised(std::min(lowFrequency, highFrequency)),
                                  frequencyToNormalised(std::max(lowFrequency, highFrequency)));
    correlationRangeSlider.setRange((1.0f - std::max(lowRange, highRange)) * 0.5f,
                                    (1.0f - std::min(lowRange, highRange)) * 0.5f);
}

void CorrelationView::refreshControls()
{
    const auto* zoom = processor.getParameters().getRawParameterValue(
        PluginProcessor::correlationZoomControlsParameterId);
    const auto* mode = processor.getParameters().getRawParameterValue(PluginProcessor::correlationModeParameterId);
    const auto amplitude = mode != nullptr && mode->load(std::memory_order_relaxed) >= 0.5f;
    phaseModeButton.setVisible(true);
    amplitudeModeButton.setVisible(true);
    phaseModeButton.setToggleState(! amplitude, juce::dontSendNotification);
    amplitudeModeButton.setToggleState(amplitude, juce::dontSendNotification);
    const auto showZoom = zoom == nullptr || zoom->load(std::memory_order_relaxed) >= 0.5f;
    frequencyRangeSlider.setVisible(showZoom);
    correlationRangeSlider.setVisible(showZoom);
}

void CorrelationView::updateFrequencyRangeFromSlider()
{
    frequencyLowControl.getSlider().setValue(normalisedToFrequency(frequencyRangeSlider.getRangeStart()),
                                              juce::sendNotificationSync);
    frequencyHighControl.getSlider().setValue(normalisedToFrequency(frequencyRangeSlider.getRangeEnd()),
                                               juce::sendNotificationSync);
}

void CorrelationView::updateCorrelationRangeFromSlider()
{
    rangeHighControl.getSlider().setValue(1.0f - correlationRangeSlider.getRangeStart() * 2.0f,
                                          juce::sendNotificationSync);
    rangeLowControl.getSlider().setValue(1.0f - correlationRangeSlider.getRangeEnd() * 2.0f,
                                         juce::sendNotificationSync);
}

juce::Rectangle<float> CorrelationView::getPlotBounds() const noexcept
{
    auto bounds = getLocalBounds().toFloat();
    const auto* zoom = processor.getParameters().getRawParameterValue(
        PluginProcessor::correlationZoomControlsParameterId);
    const auto showZoom = zoom == nullptr || zoom->load(std::memory_order_relaxed) >= 0.5f;
    if (showZoom)
    {
        bounds.removeFromBottom(static_cast<float>(bandRangeSliderHeight + ana::ui::gap.pixels()));
        bounds.removeFromRight(static_cast<float>(bandRangeSliderHeight + ana::ui::gap.pixels()));
    }
    return bounds;
}

float CorrelationView::frequencyToNormalised(const float frequency) noexcept
{
    return juce::jlimit(0.0f, 1.0f, std::log(std::max(20.0f, frequency) / 20.0f) / std::log(1000.0f));
}

float CorrelationView::normalisedToFrequency(const float normalised) noexcept
{
    return 20.0f * std::pow(1000.0f, juce::jlimit(0.0f, 1.0f, normalised));
}

MeterView::MeterView(PluginProcessor& processorRef)
    : processor(processorRef)
{
    partWeights = processor.getLevelPartWeights();
    peakModeButton.onClick = [this]
    {
        showPeakMeter = true;
        peakModeButton.setToggleState(true, juce::dontSendNotification);
        rmsModeButton.setToggleState(false, juce::dontSendNotification);
        repaint();
    };
    rmsModeButton.onClick = [this]
    {
        showPeakMeter = false;
        peakModeButton.setToggleState(false, juce::dontSendNotification);
        rmsModeButton.setToggleState(true, juce::dontSendNotification);
        repaint();
    };
    peakChannelModeButton.onClick = [this]
    {
        showMidSideMeters = ! showMidSideMeters;
        peakChannelModeButton.setToggleState(showMidSideMeters, juce::dontSendNotification);
        repaint();
    };
    historySviewButton.onClick = [this]
    {
        historySolo = ! historySolo;
        historySviewButton.setToggleState(historySolo, juce::dontSendNotification);
        resized();
    };
    historyHorizontalZoom.onRangeChanged = [this] { repaint(); };
    historyVerticalZoom.onRangeChanged = [this] { repaint(); };
    peakModeButton.setToggleState(true, juce::dontSendNotification);
    addAndMakeVisible(peakModeButton);
    addAndMakeVisible(rmsModeButton);
    addAndMakeVisible(peakChannelModeButton);
    addAndMakeVisible(historySviewButton);
    addAndMakeVisible(historyHorizontalZoom);
    addAndMakeVisible(historyVerticalZoom);
    startTimerHz(30);
}

void MeterView::paint(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colours::black);

    const auto minimumMeterColumnWidth = std::max(getMeterWidth() + 2, levelMeterReadoutWidth);
    const auto parts = getPartBounds();
    const auto visibleParts = getVisibleParts();
    auto peakRmsBounds = parts[0];
    auto lufsBounds = parts[1];
    const auto historyBounds = (historySolo && visibleParts[2]
        ? getLocalBounds() : parts[2]).toFloat();
    const auto scaleSideWidth = levelScaleLabelWidth + ana::ui::gap.pixels();
    const auto peakScalesVisible = peakRmsBounds.getWidth()
        >= minimumMeterColumnWidth * 2 + ana::ui::gap.pixels() + scaleSideWidth * 2;
    auto peakMeterHorizontalBounds = peakRmsBounds;
    if (peakScalesVisible)
        peakMeterHorizontalBounds.reduce(scaleSideWidth, 0);
    const auto peakMeterWidth = std::max(1,
        (peakMeterHorizontalBounds.getWidth() - ana::ui::gap.pixels()) / 2);
    const auto lufsScalesVisible = lufsBounds.getWidth()
        >= minimumMeterColumnWidth * 3 + ana::ui::gap.pixels() * 2 + scaleSideWidth * 2;
    auto lufsMeterHorizontalBounds = lufsBounds;
    if (lufsScalesVisible)
        lufsMeterHorizontalBounds.reduce(scaleSideWidth, 0);
    const auto lufsMeterWidth = std::max(1,
        (lufsMeterHorizontalBounds.getWidth() - ana::ui::gap.pixels() * 2) / 3);
    const auto peakChannelOffset = showMidSideMeters ? size_t { 2 } : size_t { 0 };

    const auto displayFont = ana::ui::makeFont();
    const auto readLevelScale = [this] (const char* parameterId, const float fallback)
    {
        if (const auto* value = processor.getParameters().getRawParameterValue(parameterId))
            return value->load(std::memory_order_relaxed);
        return fallback;
    };
    const auto peakLow = readLevelScale(PluginProcessor::levelPeakLowParameterId, -60.0f);
    const auto peakHigh = std::max(peakLow + 0.1f,
                                   readLevelScale(PluginProcessor::levelPeakHighParameterId, 0.0f));
    const auto lufsLow = readLevelScale(PluginProcessor::levelLufsLowParameterId, -60.0f);
    const auto lufsHigh = std::max(lufsLow + 0.1f,
                                   readLevelScale(PluginProcessor::levelLufsHighParameterId, 0.0f));
    const auto normalise = [] (const float value, const float low, const float high)
    {
        return juce::jlimit(0.0f, 1.0f, (value - low) / (high - low));
    };
    const auto drawScale = [&] (const juce::Rectangle<float> bounds, const float low, const float high,
                                const bool drawLeftLabels, const bool drawRightLabels,
                                const bool lufsScale)
    {
        graphics.setFont(displayFont);
        const auto span = high - low;
        const auto minorStep = span <= 12.0f ? 1 : span <= 24.0f ? 2 : span <= 48.0f ? 5 : 10;
        const auto minimumLabelSpacing = static_cast<float>(ana::ui::baseFontSize
            + ana::ui::gap.pixels() / 2);
        const auto maximumLabelIntervals = std::max(2, 1 + static_cast<int>(std::floor(
            bounds.getHeight() / std::max(1.0f, minimumLabelSpacing))));
        const auto requiredMajorStep = span / static_cast<float>(maximumLabelIntervals);
        const auto chooseMajorMultiplier = [] (const float minimumMultiplier)
        {
            for (const auto candidate : std::array<int, 8> { 1, 2, 3, 5, 10, 20, 50, 100 })
                if (static_cast<float>(candidate) >= minimumMultiplier)
                    return candidate;
            return 100;
        };
        const auto majorStep = minorStep * chooseMajorMultiplier(
            requiredMajorStep / static_cast<float>(minorStep));
        const auto highestTick = static_cast<int>(std::floor(high / static_cast<float>(minorStep))) * minorStep;
        const auto lowestTick = static_cast<int>(std::ceil(low / static_cast<float>(minorStep))) * minorStep;
        for (auto tick = highestTick; tick >= lowestTick; tick -= minorStep)
        {
            const auto value = static_cast<float>(tick);
            const auto y = bounds.getBottom() - normalise(value, low, high) * bounds.getHeight();
            const auto isMajorTick = tick % majorStep == 0;
            if (! isMajorTick)
                continue;
            const auto lineY = juce::roundToInt(y);
            graphics.setColour(ana::ui::dark);
            graphics.fillRect(juce::roundToInt(bounds.getX()), lineY,
                              juce::roundToInt(bounds.getWidth()), 1);

            const auto labelY = lineY - ana::ui::controlHeight / 2;
            const auto label = lufsScale && tick <= -120 ? juce::String("-inf")
                                                          : formatLevelScaleTick(tick);
            if (drawLeftLabels)
                graphics.drawText(label,
                                  juce::roundToInt(bounds.getX()) - ana::ui::gap.pixels() - levelScaleLabelWidth,
                                  labelY, levelScaleLabelWidth, ana::ui::controlHeight,
                                  juce::Justification::centredRight, true);
            if (drawRightLabels)
                graphics.drawText(label,
                                  juce::roundToInt(bounds.getRight()) + ana::ui::gap.pixels(),
                                  labelY, levelScaleLabelWidth, ana::ui::controlHeight,
                                  juce::Justification::centredLeft, true);
        }
    };
    const auto drawPeakRmsMeter = [&] (juce::Rectangle<int> bounds, const size_t channel,
                                       const bool drawLeftScale, const bool drawRightScale)
    {
        const auto showReadout = bounds.getHeight() >= ana::ui::controlHeight * 4;
        auto readoutBounds = bounds.removeFromBottom(ana::ui::controlHeight);
        ana::ui::gap.removeFromBottom(bounds);
        const auto barFrame = bounds;
        auto bar = barFrame.toFloat().reduced(1.0f, 0.0f);
        if (bar.isEmpty())
            return;
        graphics.setColour(ana::ui::black);
        graphics.fillRect(bar);
        const auto meterValue = showPeakMeter
            ? peakValues[channel] : rmsValues[channel];
        const auto meterTop = bar.getBottom()
            - normalise(meterValue, peakLow, peakHigh) * bar.getHeight();
        graphics.setColour(ana::ui::light);
        graphics.fillRect(bar.getX(), meterTop, bar.getWidth(), bar.getBottom() - meterTop);
        graphics.setColour(ana::ui::accent);
        graphics.fillRect(bar.getX(), meterTop, bar.getWidth(), 1.0f);
        if (showPeakMeter)
        {
            const auto holdY = bar.getBottom()
                - normalise(peakHoldValues[channel], peakLow, peakHigh) * bar.getHeight();
            graphics.setColour(ana::ui::red);
            graphics.fillRect(bar.getX(), holdY - 0.5f, bar.getWidth(), 2.0f);
        }
        drawScale(bar, peakLow, peakHigh, drawLeftScale, drawRightScale, false);
        graphics.setColour(ana::ui::grey500);
        graphics.drawRect(barFrame, 1);
        if (showReadout)
        {
            graphics.setColour(ana::ui::grey500);
            graphics.drawRect(readoutBounds, 1.0f);
            graphics.setFont(displayFont);
            graphics.setColour(ana::ui::white);
            graphics.drawText(meterValue <= -119.95f ? juce::String("-inf")
                                                     : formatReadoutLevel(meterValue), readoutTextBounds(readoutBounds),
                              juce::Justification::centred, true);
        }
    };
    const auto drawLufsMeter = [&] (juce::Rectangle<int> bounds, const juce::String& title,
                                    const float value, const float maximum, const bool drawLeftScale,
                                    const bool drawRightScale)
    {
        auto titleBounds = bounds.removeFromTop(ana::ui::controlHeight);
        graphics.setFont(displayFont);
        graphics.setColour(ana::ui::grey500);
        graphics.drawRect(titleBounds, 1);
        graphics.setColour(ana::ui::white);
        graphics.drawText(title, titleBounds, juce::Justification::centred, true);
        ana::ui::gap.removeFromTop(bounds);
        auto maximumBounds = bounds.removeFromTop(ana::ui::controlHeight);
        graphics.setColour(ana::ui::grey500);
        graphics.drawRect(maximumBounds, 1.0f);
        graphics.setColour(ana::ui::white);
        graphics.drawText(maximum <= -119.95f ? juce::String("-inf") : formatLufsReadout(maximum),
                          readoutTextBounds(maximumBounds), juce::Justification::centred, true);
        ana::ui::gap.removeFromTop(bounds);
        const auto showReadout = bounds.getHeight() >= ana::ui::controlHeight * 4;
        auto readoutBounds = bounds.removeFromBottom(ana::ui::controlHeight);
        ana::ui::gap.removeFromBottom(bounds);
        const auto barFrame = bounds;
        auto bar = barFrame.toFloat().reduced(1.0f, 0.0f);
        if (bar.isEmpty())
            return;
        graphics.setColour(ana::ui::black);
        graphics.fillRect(bar);
        const auto fillRange = [&] (const float low, const float high, const juce::Colour colour)
        {
            const auto fillLow = juce::jlimit(low, high, value);
            if (fillLow <= low)
                return;
            const auto top = bar.getBottom()
                - normalise(fillLow, lufsLow, lufsHigh) * bar.getHeight();
            const auto bottom = bar.getBottom()
                - normalise(low, lufsLow, lufsHigh) * bar.getHeight();
            graphics.setColour(colour);
            graphics.fillRect(bar.getX(), top, bar.getWidth(), bottom - top);
        };
        fillRange(-60.0f, -23.0f, ana::ui::green);
        fillRange(-23.0f, -14.0f, ana::ui::peach);
        fillRange(-14.0f, 0.0f, ana::ui::red);
        const auto markerY = bar.getBottom()
            - normalise(value, lufsLow, lufsHigh) * bar.getHeight();
        graphics.setColour(ana::ui::white);
        graphics.fillRect(bar.getX(), markerY, bar.getWidth(), 1.0f);
        drawScale(bar, lufsLow, lufsHigh, drawLeftScale, drawRightScale, true);
        graphics.setColour(ana::ui::grey500);
        graphics.drawRect(barFrame, 1);
        if (showReadout)
        {
            graphics.setColour(ana::ui::grey500);
            graphics.drawRect(readoutBounds, 1.0f);
            graphics.setFont(displayFont);
            graphics.setColour(ana::ui::white);
            graphics.drawText(value <= -119.95f ? juce::String("-inf")
                                                : formatLufsReadout(value), readoutTextBounds(readoutBounds),
                              juce::Justification::centred, true);
        }
    };

    if (! historySolo)
    {
        if (visibleParts[0])
        {
        peakRmsBounds.removeFromTop(ana::ui::controlHeight);
        ana::ui::gap.removeFromTop(peakRmsBounds);
        const auto peakGroupWidth = peakMeterHorizontalBounds.getWidth();
        auto peakLabelBounds = peakRmsBounds.removeFromTop(ana::ui::controlHeight);
        peakLabelBounds.setX(peakMeterHorizontalBounds.getX());
        peakLabelBounds.setWidth(peakGroupWidth);
        ana::ui::FixedGapRow peakLabelRow(peakLabelBounds);
        const std::array<juce::String, 2> peakLabels = showMidSideMeters
            ? std::array<juce::String, 2> { "M", "S" }
            : std::array<juce::String, 2> { "L", "R" };
        for (const auto& label : peakLabels)
        {
            auto labelBounds = peakLabelRow.takeLeft(peakMeterWidth);
            graphics.setColour(ana::ui::grey500);
            graphics.drawRect(labelBounds, 1.0f);
            graphics.setFont(displayFont);
            graphics.setColour(ana::ui::white);
            graphics.drawText(label, labelBounds, juce::Justification::centred, true);
        }
        ana::ui::gap.removeFromTop(peakRmsBounds);
        auto peakMaximumBounds = peakRmsBounds.removeFromTop(ana::ui::controlHeight);
        peakMaximumBounds.setX(peakMeterHorizontalBounds.getX());
        peakMaximumBounds.setWidth(peakGroupWidth);
        ana::ui::FixedGapRow peakMaximumRow(peakMaximumBounds);
        for (size_t channel = 0; channel < 2; ++channel)
        {
            const auto maximumBounds = peakMaximumRow.takeLeft(peakMeterWidth);
            const auto valueIndex = peakChannelOffset + channel;
            const auto maximum = showPeakMeter ? peakMaximumValues[valueIndex] : rmsMaximumValues[valueIndex];
            graphics.setColour(ana::ui::grey500);
            graphics.drawRect(maximumBounds, 1.0f);
            graphics.setColour(ana::ui::white);
            graphics.drawText(maximum <= -119.95f ? juce::String("-inf") : formatReadoutLevel(maximum),
                              readoutTextBounds(maximumBounds), juce::Justification::centred, true);
        }
        ana::ui::gap.removeFromTop(peakRmsBounds);
        peakRmsBounds.setX(peakMeterHorizontalBounds.getX());
        peakRmsBounds.setWidth(peakGroupWidth);
        ana::ui::FixedGapRow peakRow(peakRmsBounds);
        drawPeakRmsMeter(peakRow.takeLeft(peakMeterWidth), peakChannelOffset, peakScalesVisible, false);
        drawPeakRmsMeter(peakRow.takeLeft(peakMeterWidth), peakChannelOffset + 1, false, peakScalesVisible);
        }

        if (visibleParts[1])
        {
        const auto lufsGroupWidth = lufsMeterHorizontalBounds.getWidth();
        auto loudnessHeader = lufsMeterHorizontalBounds.removeFromTop(ana::ui::controlHeight);
        graphics.setColour(ana::ui::grey500);
        graphics.drawRect(loudnessHeader, 1.0f);
        graphics.setFont(displayFont);
        graphics.setColour(ana::ui::white);
        graphics.drawText("LOUDNESS", loudnessHeader, juce::Justification::centred, true);
        lufsBounds.removeFromTop(ana::ui::controlHeight);
        ana::ui::gap.removeFromTop(lufsBounds);
        lufsBounds.setX(lufsMeterHorizontalBounds.getX());
        lufsBounds.setWidth(lufsGroupWidth);
        ana::ui::FixedGapRow lufsRow(lufsBounds);
        drawLufsMeter(lufsRow.takeLeft(lufsMeterWidth), "M", momentaryLufs, momentaryMaximumLufs,
                      lufsScalesVisible, false);
        drawLufsMeter(lufsRow.takeLeft(lufsMeterWidth), "S", shortTermLufs, shortTermMaximumLufs, false, false);
        drawLufsMeter(lufsRow.takeLeft(lufsMeterWidth), "I", integratedLufs, integratedMaximumLufs,
                      false, lufsScalesVisible);
        }

        auto previousVisible = -1;
        for (size_t index = 0; index < visibleParts.size(); ++index)
        {
            if (! visibleParts[index])
                continue;
            if (previousVisible < 0)
            {
                previousVisible = static_cast<int>(index);
                continue;
            }
            const auto x = static_cast<float>(
                (parts[static_cast<size_t>(previousVisible)].getRight()
                 + parts[index].getX()) / 2);
            const auto activeSeparator = hoveredPartSeparator == previousVisible
                || draggedPartSeparator == previousVisible;
            graphics.setColour(activeSeparator ? ana::ui::white : ana::ui::grey500);
            graphics.fillRect(x, 0.0f, 1.0f, static_cast<float>(getHeight()));
            previousVisible = static_cast<int>(index);
        }
    }

    if (historyBounds.isEmpty())
        return;

    const auto* historyZoomParameter = processor.getParameters().getRawParameterValue(
        PluginProcessor::levelHistoryZoomParameterId);
    const auto historyZoomVisible = historyZoomParameter == nullptr
        || historyZoomParameter->load(std::memory_order_relaxed) >= 0.5f;
    const auto historyLayout = makeLevelHistoryLayout(historyBounds.toNearestInt(),
                                                       historyZoomVisible);
    graphics.setFont(displayFont);
    graphics.setColour(ana::ui::grey500);
    graphics.drawRect(historyLayout.header, 1.0f);
    graphics.setColour(ana::ui::white);
    graphics.drawText("HISTORY", historyLayout.header,
                      juce::Justification::centred, true);
    const auto drawHistoryMetrics = [&]
    {
        const auto drawMetric = [&] (const juce::String& label, const juce::String& readout,
                                     const juce::Rectangle<int> labelBounds,
                                     const juce::Rectangle<int> readoutBounds)
        {
            graphics.setColour(ana::ui::grey500);
            graphics.drawRect(labelBounds, 1.0f);
            graphics.drawRect(readoutBounds, 1.0f);
            graphics.setColour(ana::ui::white);
            graphics.drawText(label, labelBounds, juce::Justification::centred, true);
            graphics.drawText(readout, readoutTextBounds(readoutBounds),
                              juce::Justification::centred, true);
        };
        ana::ui::FixedGapRow metricLabels(historyLayout.metricLabelRow);
        ana::ui::FixedGapRow metricReadouts(historyLayout.metricReadoutRow);
        const auto truePeak = std::max(peakMaximumValues[0], peakMaximumValues[1]);
        drawMetric("TP", truePeak <= -119.95f ? juce::String("-inf") : formatReadoutLevel(truePeak),
                   metricLabels.takeLeft(historyMetricReadoutWidth),
                   metricReadouts.takeLeft(historyMetricReadoutWidth));
        drawMetric("LRA", formatReadoutLevel(loudnessRange),
                   metricLabels.takeLeft(historyMetricReadoutWidth),
                   metricReadouts.takeLeft(historyMetricReadoutWidth));
    };

    const auto historyEnabled = [this] (const char* parameterId)
    {
        const auto* value = processor.getParameters().getRawParameterValue(parameterId);
        return value == nullptr || value->load(std::memory_order_relaxed) >= 0.5f;
    };
    const std::array<bool, 3> visibleHistorySeries {
        historyEnabled(PluginProcessor::levelHistoryMomentaryParameterId),
        historyEnabled(PluginProcessor::levelHistoryShortTermParameterId),
        historyEnabled(PluginProcessor::levelHistoryIntegratedParameterId)
    };
    const auto historySize = loudnessHistories[0].size();
    const auto plotBounds = historyLayout.plot.toFloat();
    if (historySize < 2 || plotBounds.isEmpty())
    {
        drawHistoryMetrics();
        return;
    }

    const auto visibleStart = juce::jlimit(0.0f, 0.999f, historyHorizontalZoom.getRangeStart());
    const auto visibleEnd = juce::jlimit(visibleStart + 0.001f, 1.0f,
                                         historyHorizontalZoom.getRangeEnd());
    const auto firstPosition = visibleStart * static_cast<float>(historySize - 1);
    const auto lastPosition = visibleEnd * static_cast<float>(historySize - 1);
    const auto visibleLength = std::max(0.001f, lastPosition - firstPosition);
    const auto visibleHigh = juce::jmap(historyVerticalZoom.getRangeStart(), lufsHigh, lufsLow);
    const auto visibleLow = juce::jmap(historyVerticalZoom.getRangeEnd(), lufsHigh, lufsLow);
    const auto historyPoint = [&] (const float position, const float value)
    {
        return juce::Point<float> {
            plotBounds.getX() + (position - firstPosition) / visibleLength * plotBounds.getWidth(),
            plotBounds.getBottom() - normalise(value, visibleLow, visibleHigh) * plotBounds.getHeight()
        };
    };
    const auto drawHistorySeries = [&] (const size_t series, const juce::Colour colour,
                                        const bool fill)
    {
        const auto& history = loudnessHistories[series];
        if (! visibleHistorySeries[series] || history.size() != historySize)
            return;
        const auto valueAt = [&] (const float position)
        {
            const auto first = std::min(history.size() - 1,
                static_cast<size_t>(std::floor(position)));
            const auto second = std::min(history.size() - 1, first + 1);
            return juce::jmap(position - static_cast<float>(first), history[first], history[second]);
        };
        juce::Path path;
        path.startNewSubPath(historyPoint(firstPosition, valueAt(firstPosition)));
        const auto firstIndex = static_cast<size_t>(std::ceil(firstPosition));
        const auto lastIndex = static_cast<size_t>(std::floor(lastPosition));
        for (auto index = firstIndex; index <= lastIndex && index < history.size(); ++index)
            if (static_cast<float>(index) > firstPosition && static_cast<float>(index) < lastPosition)
                path.lineTo(historyPoint(static_cast<float>(index), history[index]));
        path.lineTo(historyPoint(lastPosition, valueAt(lastPosition)));
        if (fill)
        {
            auto fillPath = path;
            fillPath.lineTo(plotBounds.getRight(), plotBounds.getBottom());
            fillPath.lineTo(plotBounds.getX(), plotBounds.getBottom());
            fillPath.closeSubPath();
            graphics.setColour(ana::ui::dark);
            graphics.fillPath(fillPath);
        }
        graphics.setColour(colour);
        graphics.strokePath(path, juce::PathStrokeType(1.0f));
    };
    drawHistorySeries(ana::lvls::MeterProcessor::integratedHistory, ana::ui::white, true);
    drawHistorySeries(ana::lvls::MeterProcessor::shortTermHistory, ana::ui::peach, false);
    drawHistorySeries(ana::lvls::MeterProcessor::momentaryHistory, ana::ui::red, false);
    drawHistoryMetrics();

}

void MeterView::resized()
{
    const auto visibleParts = getVisibleParts();
    if (! visibleParts[2])
    {
        historySolo = false;
        historySviewButton.setToggleState(false, juce::dontSendNotification);
    }
    layoutPeakModeButtons();
    const auto historyBounds = historySolo ? getLocalBounds() : getPartBounds()[2];
    const auto* historyZoomParameter = processor.getParameters().getRawParameterValue(
        PluginProcessor::levelHistoryZoomParameterId);
    const auto historyZoomVisible = historyZoomParameter == nullptr
        || historyZoomParameter->load(std::memory_order_relaxed) >= 0.5f;
    const auto historyLayout = makeLevelHistoryLayout(historyBounds, historyZoomVisible);
    historySviewButton.setVisible(visibleParts[2]);
    historyHorizontalZoom.setVisible(visibleParts[2] && historyZoomVisible);
    historyVerticalZoom.setVisible(visibleParts[2] && historyZoomVisible);
    historySviewButton.setBounds(historyLayout.sviewButton);
    historyHorizontalZoom.setBounds(historyLayout.horizontalZoom);
    historyVerticalZoom.setBounds(historyLayout.verticalZoom);
    repaint();
}

void MeterView::centerParts()
{
    partWeights.fill(1.0f);
    processor.setLevelPartWeights(partWeights);
    draggedPartSeparator = -1;
    resized();
}

void MeterView::layoutPeakModeButtons()
{
    const auto showPeakControls = ! historySolo && getVisibleParts()[0];
    peakModeButton.setVisible(showPeakControls);
    rmsModeButton.setVisible(showPeakControls);
    peakChannelModeButton.setVisible(showPeakControls);
    if (! showPeakControls)
        return;

    const auto partBounds = getPartBounds()[0];
    const auto minimumMeterColumnWidth = std::max(getMeterWidth() + 2, levelMeterReadoutWidth);
    const auto scaleSideWidth = levelScaleLabelWidth + ana::ui::gap.pixels();
    const auto scalesVisible = partBounds.getWidth()
        >= minimumMeterColumnWidth * 2 + ana::ui::gap.pixels() + scaleSideWidth * 2;
    auto headerBounds = partBounds;
    if (scalesVisible)
        headerBounds.reduce(scaleSideWidth, 0);
    headerBounds = headerBounds.removeFromTop(ana::ui::controlHeight);
    const auto availableButtonWidth = std::max(0,
        headerBounds.getWidth() - ana::ui::gap.pixels() * 2);
    const auto preferredWidth = peakModeButton.getPreferredWidth()
        + rmsModeButton.getPreferredWidth() + peakChannelModeButton.getPreferredWidth();
    const auto extraPerButton = std::max(0, availableButtonWidth - preferredWidth) / 3;
    ana::ui::FixedGapRow buttonRow(headerBounds);
    peakModeButton.setBounds(buttonRow.takeLeft(
        peakModeButton.getPreferredWidth() + extraPerButton));
    rmsModeButton.setBounds(buttonRow.takeLeft(
        rmsModeButton.getPreferredWidth() + extraPerButton));
    peakChannelModeButton.setBounds(buttonRow.remaining());
}

int MeterView::getMeterWidth() const noexcept
{
    const auto* parameter = processor.getParameters().getRawParameterValue(
        PluginProcessor::levelMeterWidthParameterId);
    return juce::jlimit(106, 180, juce::roundToInt(
        parameter != nullptr ? parameter->load(std::memory_order_relaxed) : 106.0f));
}

std::array<bool, 3> MeterView::getVisibleParts() const noexcept
{
    const auto enabled = [this] (const char* parameterId)
    {
        const auto* value = processor.getParameters().getRawParameterValue(parameterId);
        return value == nullptr || value->load(std::memory_order_relaxed) >= 0.5f;
    };
    return {
        enabled(PluginProcessor::levelPeakVisibleParameterId),
        enabled(PluginProcessor::levelLoudnessVisibleParameterId),
        enabled(PluginProcessor::levelHistoryVisibleParameterId)
    };
}

std::array<int, 3> MeterView::getMinimumPartWidths() const noexcept
{
    const auto meterColumnWidth = std::max(getMeterWidth() + 2, levelMeterReadoutWidth);
    return {
        meterColumnWidth * 2 + ana::ui::gap.pixels(),
        meterColumnWidth * 3 + ana::ui::gap.pixels() * 2,
        historyMinimumWidth
    };
}

std::array<juce::Rectangle<int>, 3> MeterView::getPartBounds() const noexcept
{
    std::array<juce::Rectangle<int>, 3> bounds;
    const auto visible = getVisibleParts();
    const auto visibleCount = static_cast<int>(std::count(visible.begin(), visible.end(), true));
    if (visibleCount == 0)
        return bounds;

    auto remaining = getLocalBounds();
    const auto usableWidth = std::max(0, remaining.getWidth()
        - ana::ui::gap.pixels() * (visibleCount - 1));
    const auto minimumWidths = getMinimumPartWidths();
    auto minimumTotal = 0;
    auto totalWeight = 0.0f;
    for (size_t index = 0; index < visible.size(); ++index)
        if (visible[index])
        {
            minimumTotal += minimumWidths[index];
            totalWeight += partWeights[index];
        }
    std::array<int, 3> widths {};

    if (usableWidth < minimumTotal)
    {
        const auto scale = minimumTotal > 0
            ? static_cast<float>(usableWidth) / static_cast<float>(minimumTotal) : 0.0f;
        auto assignedWidth = 0;
        auto lastVisible = size_t { 0 };
        for (size_t index = 0; index < visible.size(); ++index)
            if (visible[index])
                lastVisible = index;
        for (size_t index = 0; index < visible.size(); ++index)
            if (visible[index])
            {
                widths[index] = index == lastVisible
                    ? std::max(0, usableWidth - assignedWidth)
                    : juce::roundToInt(static_cast<float>(minimumWidths[index]) * scale);
                assignedWidth += widths[index];
            }
    }
    else
    {
        std::array<bool, 3> fixed {};
        auto remainingWidth = usableWidth;
        auto remainingWeight = std::max(0.001f, totalWeight);

        for (size_t pass = 0; pass < widths.size(); ++pass)
        {
            auto fixedOne = false;
            for (size_t index = 0; index < widths.size(); ++index)
            {
                if (! visible[index] || fixed[index])
                    continue;

                const auto weightedWidth = static_cast<float>(remainingWidth)
                    * partWeights[index] / remainingWeight;
                if (weightedWidth >= static_cast<float>(minimumWidths[index]))
                    continue;

                widths[index] = minimumWidths[index];
                remainingWidth -= widths[index];
                remainingWeight -= partWeights[index];
                fixed[index] = true;
                fixedOne = true;
            }

            if (! fixedOne)
                break;
        }

        auto lastFlexible = widths.size();
        for (size_t index = 0; index < widths.size(); ++index)
            if (visible[index] && ! fixed[index])
                lastFlexible = index;

        for (size_t index = 0; index < widths.size(); ++index)
        {
            if (! visible[index] || fixed[index])
                continue;

            if (index == lastFlexible)
                widths[index] = remainingWidth;
            else
            {
                widths[index] = juce::roundToInt(static_cast<float>(remainingWidth)
                    * partWeights[index] / remainingWeight);
                remainingWidth -= widths[index];
                remainingWeight -= partWeights[index];
            }
        }
    }

    auto placed = 0;
    for (size_t index = 0; index < bounds.size(); ++index)
    {
        if (! visible[index])
            continue;
        bounds[index] = remaining.removeFromLeft(std::min(widths[index], remaining.getWidth()));
        ++placed;
        if (placed < visibleCount)
            ana::ui::gap.removeFromLeft(remaining);
    }
    return bounds;
}

int MeterView::findPartSeparator(const int x) const noexcept
{
    const auto parts = getPartBounds();
    const auto visible = getVisibleParts();
    auto previous = -1;
    for (size_t index = 0; index < visible.size(); ++index)
    {
        if (! visible[index])
            continue;
        if (previous < 0)
        {
            previous = static_cast<int>(index);
            continue;
        }
        const auto separatorX = (parts[static_cast<size_t>(previous)].getRight()
                                 + parts[index].getX()) / 2;
        if (std::abs(x - separatorX) <= ana::ui::gap.pixels())
            return previous;
        previous = static_cast<int>(index);
    }

    return -1;
}

void MeterView::mouseMove(const juce::MouseEvent& event)
{
    const auto nextHoveredSeparator = findPartSeparator(event.x);
    if (hoveredPartSeparator == nextHoveredSeparator)
        return;

    hoveredPartSeparator = nextHoveredSeparator;
    setMouseCursor(hoveredPartSeparator >= 0
        ? juce::MouseCursor::LeftRightResizeCursor
        : juce::MouseCursor::NormalCursor);
    repaint();
}

void MeterView::mouseExit(const juce::MouseEvent&)
{
    if (draggedPartSeparator >= 0)
        return;

    hoveredPartSeparator = -1;
    setMouseCursor(juce::MouseCursor::NormalCursor);
    repaint();
}

void MeterView::mouseDown(const juce::MouseEvent& event)
{
    if (event.originalComponent != this)
        return;

    const auto parts = getPartBounds();
    draggedPartSeparator = findPartSeparator(event.x);
    if (draggedPartSeparator < 0)
    {
        if (! historySolo && (parts[0].contains(event.getPosition()) || parts[1].contains(event.getPosition())))
            processor.clearLevelMeter();
        return;
    }

    draggedLeftPart = draggedPartSeparator;
    draggedRightPart = -1;
    const auto visible = getVisibleParts();
    for (auto part = draggedLeftPart + 1; part < static_cast<int>(visible.size()); ++part)
        if (visible[static_cast<size_t>(part)])
        {
            draggedRightPart = part;
            break;
        }
    if (draggedRightPart < 0)
    {
        draggedPartSeparator = -1;
        return;
    }

    dragStartX = event.x;
    for (size_t part = 0; part < dragStartWidths.size(); ++part)
        dragStartWidths[part] = parts[part].getWidth();
    setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
}

void MeterView::mouseDrag(const juce::MouseEvent& event)
{
    if (draggedPartSeparator < 0)
        return;

    const auto minimumPartWidths = getMinimumPartWidths();
    auto widths = dragStartWidths;
    const auto left = static_cast<size_t>(draggedLeftPart);
    const auto right = static_cast<size_t>(draggedRightPart);
    const auto pairWidth = dragStartWidths[left] + dragStartWidths[right];
    const auto minimumLeft = std::min(minimumPartWidths[left], pairWidth);
    const auto maximumLeft = std::max(minimumLeft, pairWidth - minimumPartWidths[right]);
    widths[left] = juce::jlimit(minimumLeft, maximumLeft,
                                dragStartWidths[left] + event.x - dragStartX);
    widths[right] = pairWidth - widths[left];

    const auto visibleParts = getVisibleParts();
    for (size_t index = 0; index < partWeights.size(); ++index)
        if (visibleParts[index])
            partWeights[index] = static_cast<float>(widths[index]);
    resized();
}

void MeterView::mouseUp(const juce::MouseEvent&)
{
    if (draggedPartSeparator >= 0)
        processor.setLevelPartWeights(partWeights);

    draggedLeftPart = -1;
    draggedRightPart = -1;
    draggedPartSeparator = -1;
    setMouseCursor(hoveredPartSeparator >= 0
        ? juce::MouseCursor::LeftRightResizeCursor
        : juce::MouseCursor::NormalCursor);
    repaint();
}

void MeterView::timerCallback()
{
    if (draggedPartSeparator < 0)
    {
        const auto storedWeights = processor.getLevelPartWeights();
        const auto localTotal = std::max(0.001f,
            partWeights[0] + partWeights[1] + partWeights[2]);
        auto weightsChanged = false;
        for (size_t index = 0; index < partWeights.size(); ++index)
            weightsChanged = weightsChanged
                || std::abs(partWeights[index] / localTotal - storedWeights[index]) > 1.0e-4f;

        if (weightsChanged)
        {
            partWeights = storedWeights;
            resized();
        }
    }

    auto* displayedMeter = &processor.getLevelMeter();
    auto displayRevision = displayedMeter->getRevision();
    auto offline = processor.isOfflineMode();
    if (offline)
    {
        const auto snapshot = processor.getOfflineAnalysisSnapshot();
        if (snapshot == nullptr || snapshot->meters == nullptr)
            return;

        displayedMeter = snapshot->meters.get();
        displayRevision = snapshot->revision;
    }

    auto& lastRevision = offline ? displayedOfflineRevision : displayedRealtimeRevision;
    if (displayRevision == lastRevision)
        return;

    lastRevision = displayRevision;
    const auto values = displayedMeter->getValues();
    peakValues = values.peakDecibels;
    rmsValues = values.rmsDecibels;
    peakMaximumValues = values.peakMaximumDecibels;
    peakHoldValues = values.peakHoldDecibels;
    rmsMaximumValues = values.rmsMaximumDecibels;
    momentaryMaximumLufs = values.momentaryMaximumLufs;
    shortTermMaximumLufs = values.shortTermMaximumLufs;
    integratedMaximumLufs = values.integratedMaximumLufs;
    loudnessRange = values.loudnessRange;
    if (offline)
    {
        momentaryLufs = values.momentaryLufs;
        shortTermLufs = values.shortTermLufs;
        integratedLufs = values.integratedLufs;
    }
    else
    {
        momentaryLufs = values.momentaryLufs;
        shortTermLufs = values.shortTermLufs;
        integratedLufs = values.integratedLufs;
    }
    for (size_t series = 0; series < loudnessHistories.size(); ++series)
        displayedMeter->copyHistory(series, loudnessHistories[series]);
    repaint();
}

juce::Rectangle<float> MeterView::getPlotBounds() const noexcept
{
    return getLocalBounds().toFloat();
}

ScopeView::ScopeView(PluginProcessor& processorRef)
    : processor(processorRef)
{
    setOpaque(false);
    bandHeightWeights.fill(1.0f);
    singleViewBand = processor.getScopeSingleViewBand();
    fullSourceView = processor.isScopeFullSourceView();

    for (size_t bandIndex = 0; bandIndex < bandModeButtons.size(); ++bandIndex)
    {
        for (size_t modeIndex = 0; modeIndex < bandModeButtons[bandIndex].size(); ++modeIndex)
        {
            auto button = std::make_unique<ControlButton>(scopeModeButtonNames[modeIndex]);
            button->onClick = [this, bandIndex, modeIndex]
            {
                const auto mode = scopeModeButtonModes[modeIndex];

                if (processor.getScopeChannelMode(bandIndex) != mode)
                {
                    processor.setScopeChannelMode(bandIndex, mode);
                    processor.setScopeBandNormalized(bandIndex, false);
                }

                refreshBandModeButtons();
            };
            addAndMakeVisible(*button);
            bandModeButtons[bandIndex][modeIndex] = std::move(button);
        }

        auto clearButton = std::make_unique<ControlButton>("CLEAR");
        clearButton->onClick = [this, bandIndex] { clearBandHistory(bandIndex); };
        addAndMakeVisible(*clearButton);
        bandClearButtons[bandIndex] = std::move(clearButton);

        auto singleViewButton = std::make_unique<ControlButton>("SVIEW");
        singleViewButton->onClick = [this, bandIndex]
        {
            if (fullSourceView)
            {
                fullSourceView = false;
                processor.setScopeFullSourceView(false);
            }
            singleViewBand = singleViewBand == static_cast<int>(bandIndex)
                ? -1
                : static_cast<int>(bandIndex);
            processor.setScopeSingleViewBand(singleViewBand);
            draggedBandSeparator = -1;
            refreshBandModeButtons();
            repaint();
        };
        addAndMakeVisible(*singleViewButton);
        bandSingleViewButtons[bandIndex] = std::move(singleViewButton);

        auto zoomSlider = std::make_unique<juce::Slider>();
        zoomSlider->setLookAndFeel(&bandZoomLookAndFeel);
        zoomSlider->setSliderStyle(juce::Slider::LinearBarVertical);
        zoomSlider->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        zoomSlider->setRange(-48.0, 96.0, 0.1);
        zoomSlider->setValue(processor.getScopeVerticalZoomDecibels(bandIndex), juce::dontSendNotification);
        zoomSlider->setSliderSnapsToMousePosition(false);
        zoomSlider->setScrollWheelEnabled(true);
        zoomSlider->setWantsKeyboardFocus(false);
        zoomSlider->setDoubleClickReturnValue(true, 0.0);
        zoomSlider->onValueChange = [this, bandIndex, slider = zoomSlider.get()]
        {
            if (! updatingBandControls)
            {
                processor.setScopeBandNormalized(bandIndex, false);
                processor.setScopeVerticalZoomDecibels(bandIndex, static_cast<float>(slider->getValue()));
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
        zoomValueLabel->setColour(juce::Label::backgroundColourId, ana::ui::field);
        zoomValueLabel->setColour(juce::Label::outlineColourId, ana::ui::grey500);
        zoomValueLabel->setBorderSize(juce::BorderSize<int>(1));
        zoomValueLabel->setTextVerticalOffset(readoutTextVerticalOffset);
        zoomValueLabel->setInterceptsMouseClicks(false, false);
        addAndMakeVisible(*zoomValueLabel);
        bandZoomValueLabels[bandIndex] = std::move(zoomValueLabel);

        auto normalizeButton = std::make_unique<ControlButton>("N");
        normalizeButton->onClick = [this, bandIndex] { normalizeBandWithZoom(bandIndex); };
        addAndMakeVisible(*normalizeButton);
        bandNormalizeButtons[bandIndex] = std::move(normalizeButton);

        auto rangeSlider = std::make_unique<RangeSlider>();
        rangeSlider->onRangeChanged = [this] { repaint(); };
        addAndMakeVisible(*rangeSlider);
        bandRangeSliders[bandIndex] = std::move(rangeSlider);

    }

    resetHistory();
    refreshBandModeButtons();
    startTimerHz(60);
}

void ScopeView::setFrozen(const bool shouldFreeze)
{
    if (frozen == shouldFreeze)
        return;

    frozen = shouldFreeze;

    if (! frozen)
    {
        readCursor = processor.getMultibandScope().getWriteCursor();
        columnSampleProgress = 0.0;
        resetColumnAccumulator();
    }
}

void ScopeView::clearHistory()
{
    if (showingOfflineSnapshot)
    {
        displayedOfflineSnapshot.reset();
        clearedBands.fill(true);
        widebandCleared = true;

        for (size_t bandIndex = 0; bandIndex < bandNormalizeButtons.size(); ++bandIndex)
            processor.setScopeBandNormalized(bandIndex, false);

        refreshBandModeButtons();
        repaint();
        return;
    }

    resetHistory();
}

void ScopeView::refreshWaveform()
{
    if (processor.isOfflineMode())
    {
        offlineAnalysisColumnCount = getWaveformColumnCount();
        showingOfflineSnapshot = true;
        offlineSnapshotRevision = 0;
        displayedOfflineSnapshot.reset();
        clearedBands.fill(false);

        for (size_t bandIndex = 0; bandIndex < bandNormalizeButtons.size(); ++bandIndex)
            processor.setScopeBandNormalized(bandIndex, false);

        if (onOfflineUpdateStatus)
            onOfflineUpdateStatus("UPDATING 00");
        processor.requestOfflineAnalysis(offlineAnalysisColumnCount, true);
    }

    repaint();
}

void ScopeView::equalizeBandHeights()
{
    bandHeightWeights.fill(1.0f);
    draggedBandSeparator = -1;
    refreshBandModeButtons();
    repaint();
}

void ScopeView::refreshDisplaySettings()
{
    if (! showingOfflineSnapshot)
        resizeHistory(getWaveformColumnCount());

    refreshBandModeButtons();
    repaint();
}

void ScopeView::setFullSourceView(const bool shouldShowFullSource)
{
    if (fullSourceView == shouldShowFullSource)
        return;

    fullSourceView = shouldShowFullSource;
    if (fullSourceView)
    {
        singleViewBand = -1;
        processor.setScopeSingleViewBand(-1);
    }

    draggedBandSeparator = -1;
    refreshBandModeButtons();
    repaint();
}

void ScopeView::timerCallback()
{
    const auto timeMilliseconds = processor.getScopeTimeMilliseconds();
    const auto waveformColumnCount = getWaveformColumnCount();
    const auto activeBandCount = processor.getActiveSplitCount() + 1;
    std::array<ana::ScopeChannelMode, ana::MultibandScope::numBands> currentModes;
    auto channelModeChanged = false;

    if (fullSourceView != processor.isScopeFullSourceView())
        setFullSourceView(processor.isScopeFullSourceView());

    for (size_t bandIndex = 0; bandIndex < currentModes.size(); ++bandIndex)
    {
        currentModes[bandIndex] = processor.getScopeChannelMode(bandIndex);

        if (historyChannelModes[bandIndex] != currentModes[bandIndex])
        {
            historyChannelModes[bandIndex] = currentModes[bandIndex];
            processor.setScopeBandNormalized(bandIndex, false);
            channelModeChanged = true;
        }
    }

    refreshBandModeButtons();

    const auto shouldShowOffline = processor.isOfflineMode();

    if (shouldShowOffline)
    {
        if (! showingOfflineSnapshot)
        {
            offlineAnalysisColumnCount = waveformColumnCount;
            showingOfflineSnapshot = true;
            offlineSnapshotRevision = 0;
            displayedOfflineSnapshot.reset();
            clearedBands.fill(false);
            widebandCleared = false;

            for (size_t bandIndex = 0; bandIndex < bandNormalizeButtons.size(); ++bandIndex)
                processor.setScopeBandNormalized(bandIndex, false);

            if (onOfflineUpdateStatus)
                onOfflineUpdateStatus("UPDATING 00");
            processor.requestOfflineAnalysis(offlineAnalysisColumnCount, true);
            repaint();
        }

        if (offlineAnalysisColumnCount == 0)
            offlineAnalysisColumnCount = waveformColumnCount;

        processor.requestOfflineAnalysis(offlineAnalysisColumnCount);

        if (processor.getAnalyzerPageState() != static_cast<int>(AnalyzerPage::scope))
            return;

        if (const auto snapshot = processor.getOfflineAnalysisSnapshot())
        {
            if (! showingOfflineSnapshot
                || offlineSnapshotRevision != snapshot->revision
                || channelModeChanged
                || historyBandCount != activeBandCount)
                renderOfflineSnapshot(snapshot);
        }

        return;
    }

    if (showingOfflineSnapshot)
    {
        showingOfflineSnapshot = false;
        offlineAnalysisColumnCount = 0;
        offlineSnapshotRevision = 0;
        displayedOfflineSnapshot.reset();
        widebandCleared = false;

        for (size_t bandIndex = 0; bandIndex < bandNormalizeButtons.size(); ++bandIndex)
            processor.setScopeBandNormalized(bandIndex, false);

        if (onOfflineUpdateStatus)
            onOfflineUpdateStatus({});

        readCursor = processor.getMultibandScope().getWriteCursor();
        columnSampleProgress = 0.0;
        resetColumnAccumulator();
        repaint();
        return;
    }

    if (frozen)
        return;

    if (juce::Time::getMillisecondCounterHiRes() < realtimeResumeTimeMilliseconds)
    {
        readCursor = processor.getMultibandScope().getWriteCursor();
        columnSampleProgress = 0.0;
        resetColumnAccumulator();
        return;
    }

    if (std::abs(historyTimeMilliseconds - timeMilliseconds) > 0.001
        || historyBandCount != activeBandCount)
    {
        resetHistory();
        return;
    }

    if (getWidth() <= 0 || historyEnvelopes.front().front().minimums.empty())
        return;

    auto& scope = processor.getMultibandScope();
    scope.copySince(incomingSamples, readCursor);

    const auto sampleCount = incomingSamples.front().front().size();
    if (sampleCount == 0)
        return;

    const auto visibleHistoryColumns = std::max<size_t>(
        1, historyEnvelopes.front().front().minimums.size());
    const auto samplesPerColumn = std::max(
        0.001,
        scope.getSampleRate() * timeMilliseconds * 0.001
            / static_cast<double>(visibleHistoryColumns));
    auto appendedColumn = false;

    for (size_t sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex)
    {
        const auto widebandModes = getRealtimeWidebandModeSamples(sampleIndex);
        for (size_t modeIndex = 0; modeIndex < widebandModes.size(); ++modeIndex)
        {
            widebandColumnMinimums[modeIndex] = std::min(
                widebandColumnMinimums[modeIndex], widebandModes[modeIndex]);
            widebandColumnMaximums[modeIndex] = std::max(
                widebandColumnMaximums[modeIndex], widebandModes[modeIndex]);
        }

        for (size_t bandIndex = 0; bandIndex < activeBandCount; ++bandIndex)
        {
            const auto modes = getRealtimeModeSamples(bandIndex, sampleIndex);

            for (size_t modeIndex = 0; modeIndex < modes.size(); ++modeIndex)
            {
                columnMinimums[bandIndex][modeIndex] = std::min(
                    columnMinimums[bandIndex][modeIndex], modes[modeIndex]);
                columnMaximums[bandIndex][modeIndex] = std::max(
                    columnMaximums[bandIndex][modeIndex], modes[modeIndex]);
            }
        }

        columnSampleProgress += 1.0;
        const auto columnsToAppend = static_cast<size_t>(columnSampleProgress / samplesPerColumn);

        if (columnsToAppend == 0)
            continue;

        for (size_t columnIndex = 0; columnIndex < columnsToAppend; ++columnIndex)
            appendHistoryColumn(activeBandCount);

        columnSampleProgress -= static_cast<double>(columnsToAppend) * samplesPerColumn;
        resetColumnAccumulator();
        appendedColumn = true;
    }

    if (appendedColumn)
        repaint();
}

void ScopeView::paint(juce::Graphics& graphics)
{
    const auto activeBandCount = processor.getActiveSplitCount() + 1;
    const auto filledStyle = processor.isScopeFilledStyle();
    graphics.setColour(ana::ui::opacityShade(processor.getScopeOpacity()));

    for (size_t bandIndex = 0; bandIndex < activeBandCount; ++bandIndex)
    {
        if ((fullSourceView && widebandCleared)
            || (! fullSourceView && clearedBands[bandIndex]))
            continue;

        const auto laneBounds = getBandBounds(bandIndex, activeBandCount);

        if (laneBounds.isEmpty())
            continue;

        auto waveformBounds = laneBounds;
        const auto zoomSlidersVisible = shouldShowZoomSliders(bandIndex, activeBandCount);
        if (zoomSlidersVisible)
        {
            waveformBounds.setBottom(std::max(waveformBounds.getY() + 1.0f,
                                              waveformBounds.getBottom()
                                                  - static_cast<float>(bandRangeSliderHeight + ana::ui::gap.pixels())));
            waveformBounds.setRight(std::max(
                waveformBounds.getX() + 1.0f,
                static_cast<float>(getWidth() - waveformRightInset)));
        }

        const auto rangeStart = bandRangeSliders[bandIndex]->getRangeStart();
        const auto rangeEnd = bandRangeSliders[bandIndex]->getRangeEnd();
        const auto displayedModes = getDisplayedModes(processor.getScopeChannelMode(bandIndex));

        for (size_t displayIndex = 0; displayIndex < displayedModes.count; ++displayIndex)
        {
            auto displayBounds = waveformBounds;

            if (displayedModes.count == 2)
            {
                const auto halfHeight = waveformBounds.getHeight() * 0.5f;
                displayBounds.setY(waveformBounds.getY() + halfHeight * static_cast<float>(displayIndex));
                displayBounds.setHeight(displayIndex == 0
                    ? halfHeight
                    : waveformBounds.getBottom() - displayBounds.getY());
            }

            const auto modeIndex = displayedModes.indices[displayIndex];
            const auto* envelope = showingOfflineSnapshot && displayedOfflineSnapshot != nullptr
                ? (fullSourceView
                    ? &displayedOfflineSnapshot->wideband[modeIndex]
                    : &displayedOfflineSnapshot->bands[bandIndex][modeIndex])
                : ! showingOfflineSnapshot
                    ? (fullSourceView
                        ? &widebandHistoryEnvelopes[modeIndex]
                        : &historyEnvelopes[bandIndex][modeIndex])
                    : nullptr;

            if (envelope != nullptr)
                drawWaveformEnvelope(graphics, envelope->minimums, envelope->maximums, displayBounds,
                                     rangeStart, rangeEnd,
                                     processor.getScopeVerticalZoomDecibels(bandIndex),
                                     filledStyle);
        }
    }

    graphics.setColour(ana::ui::dark);

    for (size_t bandIndex = 0; bandIndex < activeBandCount; ++bandIndex)
    {
        auto waveformBounds = getBandBounds(bandIndex, activeBandCount);

        if (waveformBounds.isEmpty())
            continue;

        const auto zoomSlidersVisible = shouldShowZoomSliders(bandIndex, activeBandCount);

        if (zoomSlidersVisible)
            waveformBounds.setBottom(std::max(waveformBounds.getY() + 1.0f,
                                              waveformBounds.getBottom()
                                                  - static_cast<float>(bandRangeSliderHeight + ana::ui::gap.pixels())));

        const auto lineWidth = zoomSlidersVisible
            ? std::max(0, getWidth() - waveformRightInset)
            : getWidth();
        const auto displayedModes = getDisplayedModes(processor.getScopeChannelMode(bandIndex));

        for (size_t displayIndex = 0; displayIndex < displayedModes.count; ++displayIndex)
        {
            const auto centre = displayedModes.count == 1
                ? waveformBounds.getCentreY()
                : waveformBounds.getY()
                    + waveformBounds.getHeight() * (static_cast<float>(displayIndex) + 0.5f) * 0.5f;
            graphics.fillRect(0, juce::roundToInt(centre), lineWidth, 1);
        }

        if (displayedModes.count == 2)
        {
            graphics.setColour(ana::ui::accent);
            graphics.fillRect(0, juce::roundToInt(waveformBounds.getCentreY()), lineWidth, 1);
            graphics.setColour(ana::ui::dark);
        }
    }

    graphics.setColour(ana::ui::accent);

    for (size_t bandIndex = 1;
         ! fullSourceView && singleViewBand < 0 && bandIndex < activeBandCount;
         ++bandIndex)
    {
        const auto y = juce::roundToInt(getBandBounds(bandIndex, activeBandCount).getY());
        graphics.fillRect(0, y, getWidth(), 1);
    }
}

void ScopeView::resized()
{
    const auto sizeChanged = lastComponentWidth != getWidth()
        || lastComponentHeight != getHeight();
    lastComponentWidth = getWidth();
    lastComponentHeight = getHeight();

    if (sizeChanged && historyContainsRecordedData && ! showingOfflineSnapshot)
    {
        realtimeResumeTimeMilliseconds = juce::Time::getMillisecondCounterHiRes() + 120.0;
        readCursor = processor.getMultibandScope().getWriteCursor();
        columnSampleProgress = 0.0;
        resetColumnAccumulator();
    }

    if (! showingOfflineSnapshot && ! historyContainsRecordedData)
        resizeHistory(getWaveformColumnCount());

    refreshBandModeButtons();
    repaint();
}

void ScopeView::resetHistory()
{
    showingOfflineSnapshot = false;
    offlineAnalysisColumnCount = 0;
    offlineSnapshotRevision = 0;
    displayedOfflineSnapshot.reset();
    historyTimeMilliseconds = processor.getScopeTimeMilliseconds();
    historyBandCount = processor.getActiveSplitCount() + 1;
    historyContainsRecordedData = false;
    clearedBands.fill(false);
    widebandCleared = false;

    for (size_t bandIndex = 0; bandIndex < historyChannelModes.size(); ++bandIndex)
        historyChannelModes[bandIndex] = processor.getScopeChannelMode(bandIndex);

    readCursor = processor.getMultibandScope().getWriteCursor();
    columnSampleProgress = 0.0;
    resetColumnAccumulator();

    const auto historyColumnCount = getWaveformColumnCount();

    for (auto& band : historyEnvelopes)
        for (auto& envelope : band)
        {
            envelope.minimums.assign(historyColumnCount, std::numeric_limits<float>::max());
            envelope.maximums.assign(historyColumnCount, std::numeric_limits<float>::lowest());
        }

    for (auto& envelope : widebandHistoryEnvelopes)
    {
        envelope.minimums.assign(historyColumnCount, std::numeric_limits<float>::max());
        envelope.maximums.assign(historyColumnCount, std::numeric_limits<float>::lowest());
    }

    repaint();
}

void ScopeView::resizeHistory(const size_t newColumnCount)
{
    const auto targetColumnCount = std::max<size_t>(1, newColumnCount);
    const auto resizeEnvelopes = [targetColumnCount] (auto& envelopes)
    {
        for (auto& envelope : envelopes)
        {
            const auto oldMinimums = std::move(envelope.minimums);
            const auto oldMaximums = std::move(envelope.maximums);
            envelope.minimums.assign(targetColumnCount, std::numeric_limits<float>::max());
            envelope.maximums.assign(targetColumnCount, std::numeric_limits<float>::lowest());

            if (oldMinimums.empty() || oldMaximums.size() != oldMinimums.size())
                continue;

            const auto sourceColumnCount = oldMinimums.size();

            for (size_t targetColumn = 0; targetColumn < targetColumnCount; ++targetColumn)
            {
                const auto firstSourceColumn = std::min(sourceColumnCount - 1,
                    targetColumn * sourceColumnCount / targetColumnCount);
                const auto endSourceColumn = std::max(firstSourceColumn + 1,
                    std::min(sourceColumnCount,
                        static_cast<size_t>(std::ceil(
                            static_cast<double>(targetColumn + 1)
                            * static_cast<double>(sourceColumnCount)
                            / static_cast<double>(targetColumnCount)))));

                for (auto sourceColumn = firstSourceColumn;
                     sourceColumn < endSourceColumn;
                     ++sourceColumn)
                {
                    if (oldMinimums[sourceColumn] <= oldMaximums[sourceColumn])
                    {
                        envelope.minimums[targetColumn] = std::min(
                            envelope.minimums[targetColumn], oldMinimums[sourceColumn]);
                        envelope.maximums[targetColumn] = std::max(
                            envelope.maximums[targetColumn], oldMaximums[sourceColumn]);
                    }
                }
            }
        }
    };

    for (auto& band : historyEnvelopes)
        resizeEnvelopes(band);
    resizeEnvelopes(widebandHistoryEnvelopes);

    repaint();
}

void ScopeView::clearBandHistory(const size_t bandIndex)
{
    if (bandIndex >= historyBandCount)
        return;

    const auto bandBounds = getBandBounds(bandIndex, historyBandCount).toNearestInt();
    if (fullSourceView)
    {
        for (auto& envelope : widebandHistoryEnvelopes)
        {
            envelope.minimums.assign(envelope.minimums.size(), std::numeric_limits<float>::max());
            envelope.maximums.assign(envelope.maximums.size(), std::numeric_limits<float>::lowest());
        }
        widebandCleared = showingOfflineSnapshot;
        widebandColumnMinimums.fill(std::numeric_limits<float>::max());
        widebandColumnMaximums.fill(std::numeric_limits<float>::lowest());
        processor.setScopeBandNormalized(0, false);
        refreshBandModeButtons();
        repaint(bandBounds);
        return;
    }

    for (auto& envelope : historyEnvelopes[bandIndex])
    {
        envelope.minimums.assign(envelope.minimums.size(), std::numeric_limits<float>::max());
        envelope.maximums.assign(envelope.maximums.size(), std::numeric_limits<float>::lowest());
    }
    clearedBands[bandIndex] = showingOfflineSnapshot;
    columnMinimums[bandIndex].fill(std::numeric_limits<float>::max());
    columnMaximums[bandIndex].fill(std::numeric_limits<float>::lowest());
    processor.setScopeBandNormalized(bandIndex, false);
    refreshBandModeButtons();
    repaint(bandBounds);
}

void ScopeView::resetColumnAccumulator()
{
    for (auto& band : columnMinimums)
        band.fill(std::numeric_limits<float>::max());
    for (auto& band : columnMaximums)
        band.fill(std::numeric_limits<float>::lowest());
    widebandColumnMinimums.fill(std::numeric_limits<float>::max());
    widebandColumnMaximums.fill(std::numeric_limits<float>::lowest());
}

void ScopeView::normalizeBandWithZoom(const size_t bandIndex)
{
    if (! processor.isOfflineMode()
        || ! showingOfflineSnapshot
        || displayedOfflineSnapshot == nullptr
        || bandIndex >= historyBandCount
        || (fullSourceView ? widebandCleared : clearedBands[bandIndex]))
        return;

    if (processor.isScopeBandNormalized(bandIndex))
    {
        processor.setScopeBandNormalized(bandIndex, false);
        processor.setScopeVerticalZoomDecibels(bandIndex, 0.0f);
        refreshBandModeButtons();
        repaint();
        return;
    }

    auto peak = 0.0f;
    size_t validColumnCount = 0;
    const auto displayedModes = getDisplayedModes(processor.getScopeChannelMode(bandIndex));

    for (size_t displayIndex = 0; displayIndex < displayedModes.count; ++displayIndex)
    {
        const auto& envelope = fullSourceView
            ? displayedOfflineSnapshot->wideband[displayedModes.indices[displayIndex]]
            : displayedOfflineSnapshot->bands[bandIndex][displayedModes.indices[displayIndex]];

        if (envelope.minimums.empty()
            || envelope.maximums.size() != envelope.minimums.size())
            continue;

        for (size_t index = 0; index < envelope.minimums.size(); ++index)
        {
            if (envelope.minimums[index] <= envelope.maximums[index])
            {
                peak = std::max(peak,
                    std::max(std::abs(envelope.minimums[index]),
                             std::abs(envelope.maximums[index])));
                ++validColumnCount;
            }
        }
    }

    if (validColumnCount == 0 || peak <= 1.0e-6f)
        return;

    const auto normalizationZoom = juce::jlimit(
        -48.0f, 96.0f, juce::Decibels::gainToDecibels(1.0f / peak));
    processor.setScopeVerticalZoomDecibels(bandIndex, normalizationZoom);
    processor.setScopeBandNormalized(bandIndex, true);
    refreshBandModeButtons();
    repaint();
}

void ScopeView::refreshBandModeButtons()
{
    const auto activeBandCount = processor.getActiveSplitCount() + 1;
    const auto persistedSingleViewBand = fullSourceView ? -1 : processor.getScopeSingleViewBand();

    if (persistedSingleViewBand != singleViewBand)
        singleViewBand = persistedSingleViewBand;

    if (singleViewBand >= static_cast<int>(activeBandCount))
    {
        singleViewBand = -1;
        processor.setScopeSingleViewBand(-1);
    }

    constexpr int buttonHeight = ana::ui::controlHeight;
    constexpr int zoomSliderWidth = bandZoomSliderWidth;
    constexpr int zoomValueWidth = bandZoomValueWidth;
    const auto offlineMode = processor.isOfflineMode();
    const auto showZoomControls = processor.areScopeZoomControlsVisible();
    const auto showMonitorControls = processor.areScopeMonitorControlsVisible();
    const auto showOtherControls = processor.areScopeOtherControlsVisible();
    const auto normalizationAvailable = offlineMode
        && showingOfflineSnapshot
        && displayedOfflineSnapshot != nullptr;
    const juce::ScopedValueSetter<bool> controlUpdate(updatingBandControls, true);

    for (size_t bandIndex = 0; bandIndex < bandModeButtons.size(); ++bandIndex)
    {
        const auto isActive = bandIndex < activeBandCount;
        const auto isVisibleBand = isActive
            && (fullSourceView
                ? bandIndex == 0
                : (singleViewBand < 0 || singleViewBand == static_cast<int>(bandIndex)));
        const auto selectedMode = processor.getScopeChannelMode(bandIndex);
        const auto verticalZoomDecibels = processor.getScopeVerticalZoomDecibels(bandIndex);
        const auto normalized = offlineMode && processor.isScopeBandNormalized(bandIndex);
        const auto laneBounds = isVisibleBand
            ? getBandBounds(bandIndex, activeBandCount).toNearestInt()
            : juce::Rectangle<int>();
        const auto showZoomSliders = isVisibleBand
            && shouldShowZoomSliders(bandIndex, activeBandCount);
        const auto laneTopInset = laneBounds.getY() == 0 ? 0 : ana::ui::gap.pixels();
        const auto controlsY = laneBounds.getY() + laneTopInset;
        ana::ui::FixedGapRow controlsRow({ 0, controlsY, getWidth(), buttonHeight });

        for (size_t modeIndex = 0; modeIndex < bandModeButtons[bandIndex].size(); ++modeIndex)
        {
            auto& button = *bandModeButtons[bandIndex][modeIndex];
            button.setVisible(isVisibleBand && showMonitorControls);
            button.setToggleState(selectedMode == scopeModeButtonModes[modeIndex],
                                  juce::dontSendNotification);

            if (isVisibleBand && showMonitorControls)
                button.setBounds(controlsRow.takeLeft(button.getPreferredWidth()));
        }

        auto& clearButton = *bandClearButtons[bandIndex];
        clearButton.setVisible(isVisibleBand && showOtherControls);

        if (isVisibleBand && showOtherControls)
            clearButton.setBounds(controlsRow.takeLeft(clearButton.getPreferredWidth()));

        auto& singleViewButton = *bandSingleViewButtons[bandIndex];
        singleViewButton.setVisible(isVisibleBand && showOtherControls && ! fullSourceView);
        singleViewButton.setToggleState(singleViewBand == static_cast<int>(bandIndex),
                                        juce::dontSendNotification);

        if (isVisibleBand && showOtherControls)
            singleViewButton.setBounds(controlsRow.takeLeft(singleViewButton.getPreferredWidth()));

        auto& zoomSlider = *bandZoomSliders[bandIndex];
        auto& zoomValueLabel = *bandZoomValueLabels[bandIndex];
        auto& normalizeButton = *bandNormalizeButtons[bandIndex];
        zoomSlider.setVisible(showZoomSliders);
        zoomValueLabel.setVisible(isVisibleBand && showZoomControls);
        normalizeButton.setVisible(isVisibleBand && showZoomControls);
        normalizeButton.setEnabled(normalizationAvailable
                                   && ! (fullSourceView ? widebandCleared : clearedBands[bandIndex]));
        zoomSlider.setValue(verticalZoomDecibels, juce::dontSendNotification);
        zoomSlider.setTooltip("ZOOM " + formatZoomValue(verticalZoomDecibels));
        zoomValueLabel.setText(formatZoomValue(verticalZoomDecibels), juce::dontSendNotification);
        normalizeButton.setToggleState(normalized, juce::dontSendNotification);

        auto& rangeSlider = *bandRangeSliders[bandIndex];
        rangeSlider.setVisible(showZoomSliders);

        if (isVisibleBand && showZoomControls)
        {
            const auto buttonY = controlsY;
            const auto sliderX = showZoomSliders
                ? std::max(0, getWidth() - zoomSliderWidth)
                : getWidth();
            const auto zoomValueX = sliderX - (showZoomSliders ? ana::ui::gap.pixels() : 0)
                - zoomValueWidth;
            const auto normalizeButtonWidth = normalizeButton.getPreferredWidth();
            const auto normalizeButtonX = zoomValueX - ana::ui::gap.pixels() - normalizeButtonWidth;

            zoomValueLabel.setBounds(zoomValueX, buttonY, zoomValueWidth, buttonHeight);
            normalizeButton.setBounds(normalizeButtonX, buttonY, normalizeButtonWidth, buttonHeight);

            if (showZoomSliders)
            {
                const auto rangeBounds = juce::Rectangle<int>(
                    0,
                    laneBounds.getBottom() - ana::ui::gap.pixels() - bandRangeSliderHeight,
                    std::max(1, sliderX - ana::ui::gap.pixels()),
                    bandRangeSliderHeight);
                rangeSlider.setBounds(rangeBounds);
                zoomSlider.setBounds(sliderX, buttonY, zoomSliderWidth,
                                     std::max(1, rangeBounds.getBottom() - buttonY));
            }
        }
    }
}

juce::Rectangle<float> ScopeView::getBandBounds(
    const size_t bandIndex, const size_t activeBandCount) const noexcept
{
    if (activeBandCount == 0 || bandIndex >= activeBandCount)
        return {};

    if (fullSourceView)
    {
        if (bandIndex != 0)
            return {};

        return { 0.0f, 0.0f, static_cast<float>(std::max(0, getWidth() - 1)),
                 static_cast<float>(getHeight()) };
    }

    if (singleViewBand >= 0)
    {
        if (singleViewBand != static_cast<int>(bandIndex))
            return {};

        return { 0.0f, 0.0f, static_cast<float>(std::max(0, getWidth() - 1)),
                 static_cast<float>(getHeight()) };
    }

    auto totalWeight = 0.0f;
    auto precedingWeight = 0.0f;

    for (size_t index = 0; index < activeBandCount; ++index)
    {
        const auto weight = std::max(0.001f, bandHeightWeights[index]);
        totalWeight += weight;

        if (index < bandIndex)
            precedingWeight += weight;
    }

    const auto availableHeight = static_cast<float>(getHeight());
    const auto fixedHeight = std::min(static_cast<float>(minimumBandHeight),
                                      availableHeight / static_cast<float>(activeBandCount));
    const auto distributableHeight = std::max(0.0f,
        availableHeight - fixedHeight * static_cast<float>(activeBandCount));
    const auto top = fixedHeight * static_cast<float>(bandIndex)
        + distributableHeight * precedingWeight / totalWeight;
    const auto bottomWeight = precedingWeight + std::max(0.001f, bandHeightWeights[bandIndex]);
    const auto bottom = bandIndex + 1 == activeBandCount
        ? availableHeight
        : fixedHeight * static_cast<float>(bandIndex + 1)
            + distributableHeight * bottomWeight / totalWeight;
    return { 0.0f, top, static_cast<float>(std::max(0, getWidth() - 1)),
             std::max(0.0f, bottom - top) };
}

bool ScopeView::shouldShowZoomSliders(
    const size_t bandIndex, const size_t activeBandCount) const noexcept
{
    return processor.areScopeZoomControlsVisible()
        && getBandBounds(bandIndex, activeBandCount).getHeight()
            >= static_cast<float>(minimumBandHeight);
}

bool ScopeView::hasVisibleZoomSliders() const noexcept
{
    const auto activeBandCount = processor.getActiveSplitCount() + 1;
    for (size_t bandIndex = 0; bandIndex < activeBandCount; ++bandIndex)
        if (shouldShowZoomSliders(bandIndex, activeBandCount))
            return true;

    return false;
}

size_t ScopeView::getWaveformColumnCount() const noexcept
{
    const auto drawableWidth = hasVisibleZoomSliders()
        ? getWidth() - waveformRightInset
        : getWidth() - 1;
    return static_cast<size_t>(std::max(1, drawableWidth));
}

int ScopeView::findBandSeparator(
    const int y, const size_t activeBandCount) const noexcept
{
    if (fullSourceView || singleViewBand >= 0)
        return -1;

    constexpr int hitRadius = 5;

    for (size_t bandIndex = 1; bandIndex < activeBandCount; ++bandIndex)
    {
        const auto separatorY = juce::roundToInt(getBandBounds(bandIndex, activeBandCount).getY());

        if (std::abs(y - separatorY) <= hitRadius)
            return static_cast<int>(bandIndex - 1);
    }

    return -1;
}

void ScopeView::mouseMove(const juce::MouseEvent& event)
{
    const auto activeBandCount = processor.getActiveSplitCount() + 1;
    setMouseCursor(findBandSeparator(event.y, activeBandCount) >= 0
        ? juce::MouseCursor::UpDownResizeCursor
        : juce::MouseCursor::NormalCursor);
}

void ScopeView::mouseExit(const juce::MouseEvent&)
{
    if (draggedBandSeparator < 0)
        setMouseCursor(juce::MouseCursor::NormalCursor);
}

void ScopeView::mouseDown(const juce::MouseEvent& event)
{
    draggedBandSeparator = findBandSeparator(event.y, processor.getActiveSplitCount() + 1);

    if (draggedBandSeparator >= 0)
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
}

void ScopeView::mouseDrag(const juce::MouseEvent& event)
{
    const auto activeBandCount = processor.getActiveSplitCount() + 1;

    if (draggedBandSeparator < 0
        || static_cast<size_t>(draggedBandSeparator + 1) >= activeBandCount)
        return;

    const auto upperIndex = static_cast<size_t>(draggedBandSeparator);
    const auto lowerIndex = upperIndex + 1;
    const auto upperBounds = getBandBounds(upperIndex, activeBandCount);
    const auto lowerBounds = getBandBounds(lowerIndex, activeBandCount);
    const auto combinedTop = upperBounds.getY();
    const auto combinedBottom = lowerBounds.getBottom();
    const auto combinedHeight = combinedBottom - combinedTop;

    if (combinedHeight <= 1.0f)
        return;

    const auto minimumHeight = std::min(static_cast<float>(minimumBandHeight),
                                        combinedHeight * 0.5f);
    const auto separatorY = juce::jlimit(combinedTop + minimumHeight,
                                         combinedBottom - minimumHeight,
                                         static_cast<float>(event.y));
    const auto distributableHeight = combinedHeight - minimumHeight * 2.0f;

    if (distributableHeight <= 0.0f)
        return;

    const auto upperRatio = (separatorY - combinedTop - minimumHeight) / distributableHeight;
    const auto combinedWeight = bandHeightWeights[upperIndex] + bandHeightWeights[lowerIndex];
    bandHeightWeights[upperIndex] = combinedWeight * upperRatio;
    bandHeightWeights[lowerIndex] = combinedWeight * (1.0f - upperRatio);
    refreshBandModeButtons();
    repaint();
}

void ScopeView::mouseUp(const juce::MouseEvent& event)
{
    draggedBandSeparator = -1;
    mouseMove(event);
}

std::array<float, ana::OfflineAnalysisSnapshot::numChannelModes>
ScopeView::getRealtimeModeSamples(const size_t bandIndex,
                                                   const size_t sampleIndex) const noexcept
{
    const auto left = incomingSamples[bandIndex][0][sampleIndex];
    const auto right = incomingSamples[bandIndex][1][sampleIndex];
    return { left, right, 0.5f * (left + right), 0.5f * (left - right) };
}

std::array<float, ana::OfflineAnalysisSnapshot::numChannelModes>
ScopeView::getRealtimeWidebandModeSamples(const size_t sampleIndex) const noexcept
{
    const auto left = incomingSamples.wideband[0][sampleIndex];
    const auto right = incomingSamples.wideband[1][sampleIndex];
    return { left, right, 0.5f * (left + right), 0.5f * (left - right) };
}

void ScopeView::appendHistoryColumn(const size_t activeBandCount)
{
    if (historyEnvelopes.front().front().minimums.empty())
        return;

    for (size_t bandIndex = 0; bandIndex < activeBandCount; ++bandIndex)
    {
        for (size_t modeIndex = 0; modeIndex < historyEnvelopes[bandIndex].size(); ++modeIndex)
        {
            auto& envelope = historyEnvelopes[bandIndex][modeIndex];

            if (envelope.minimums.size() > 1)
            {
                std::move(envelope.minimums.begin() + 1,
                          envelope.minimums.end(), envelope.minimums.begin());
                std::move(envelope.maximums.begin() + 1,
                          envelope.maximums.end(), envelope.maximums.begin());
            }

            envelope.minimums.back() = columnMinimums[bandIndex][modeIndex];
            envelope.maximums.back() = columnMaximums[bandIndex][modeIndex];
        }
        clearedBands[bandIndex] = false;
    }

    for (size_t modeIndex = 0; modeIndex < widebandHistoryEnvelopes.size(); ++modeIndex)
    {
        auto& envelope = widebandHistoryEnvelopes[modeIndex];
        if (envelope.minimums.size() > 1)
        {
            std::move(envelope.minimums.begin() + 1,
                      envelope.minimums.end(), envelope.minimums.begin());
            std::move(envelope.maximums.begin() + 1,
                      envelope.maximums.end(), envelope.maximums.begin());
        }

        envelope.minimums.back() = widebandColumnMinimums[modeIndex];
        envelope.maximums.back() = widebandColumnMaximums[modeIndex];
    }
    widebandCleared = false;

    historyContainsRecordedData = true;
}

void ScopeView::renderOfflineSnapshot(
    std::shared_ptr<const ana::OfflineAnalysisSnapshot> snapshot)
{
    if (snapshot == nullptr)
        return;

    const auto receivedNewRevision = offlineSnapshotRevision != snapshot->revision;
    historyBandCount = std::min(snapshot->activeBandCount,
                                processor.getActiveSplitCount() + 1);
    offlineSnapshotRevision = snapshot->revision;
    showingOfflineSnapshot = true;
    displayedOfflineSnapshot = std::move(snapshot);
    clearedBands.fill(false);
    widebandCleared = false;

    if (receivedNewRevision)
    {
        for (size_t bandIndex = 0; bandIndex < bandNormalizeButtons.size(); ++bandIndex)
            processor.setScopeBandNormalized(bandIndex, false);

        refreshBandModeButtons();

        if (onOfflineUpdateStatus)
            onOfflineUpdateStatus(
                "UPDATED");
    }

    repaint();
}

SettingsPanel::SettingsPanel(PluginProcessor& processorRef)
    : processor(processorRef),
      styleControl(processorRef.getParameters(), PluginProcessor::scopeStyleParameterId,
                   "STYLE",
                   [] (const double value)
                   {
                       return value < 0.5 ? juce::String("FILLED") : juce::String("OUTLINE");
                   }),
      opacityControl(processorRef.getParameters(), PluginProcessor::scopeOpacityParameterId,
                     "OPACITY",
                     [] (const double value)
                     {
                         return juce::String(juce::roundToInt(value));
                     }),
      timeControl(processorRef.getParameters(), PluginProcessor::scopeTimeParameterId,
                  "TIME",
                  [] (const double value)
                  {
                      const auto seconds = value / 1000.0;
                      return juce::String(seconds, seconds < 10.0 ? 1 : 0);
                  }),
      timeNoteControl(processorRef.getParameters(), PluginProcessor::scopeNoteLengthParameterId,
                      "TIME",
                      [] (const double value)
                      {
                          static const juce::StringArray choices {
                              "1/16", "1/8", "1/4", "1/2", "1/1",
                              "2/1", "4/1", "8/1", "16/1"
                          };
                          return choices[juce::jlimit(0, choices.size() - 1,
                                                     juce::roundToInt(value))];
                      }),
      timeBaseControl(processorRef.getParameters(), PluginProcessor::scopeTimeBaseParameterId,
                      "TIME-BASE",
                      [] (const double value)
                      {
                          return value < 0.5 ? juce::String("MS") : juce::String("NOTE");
                  }),
      frequencyBlockSizeControl(processorRef.getParameters(), PluginProcessor::frequencyBlockSizeParameterId,
                                "BLOCK-SIZE", formatBlockSizeChoice),
      frequencyOverlapControl(processorRef.getParameters(), PluginProcessor::frequencyOverlapParameterId,
                              "OVERLAP", [] (const double value) { return juce::String(juce::roundToInt(value * 100.0)); }),
      frequencyAverageTimeControl(processorRef.getParameters(), PluginProcessor::frequencyAverageTimeParameterId,
                                  "AVG-TIME", [] (const double value) { return juce::String(juce::roundToInt(value)); }),
      frequencySmoothingControl(processorRef.getParameters(), PluginProcessor::frequencySmoothingParameterId,
                                "SMOOTHING", [] (const double value) { return juce::String(juce::roundToInt(value)); }),
      frequencyFirstSpectrumTypeControl(processorRef.getParameters(), PluginProcessor::frequencyFirstSpectrumTypeParameterId,
                                        "1ST-TYPE", formatSpectrumTypeChoice),
      frequencySecondSpectrumTypeControl(processorRef.getParameters(), PluginProcessor::frequencySecondSpectrumTypeParameterId,
                                         "2ND-TYPE", formatSpectrumTypeChoice),
      frequencySlopeControl(processorRef.getParameters(), PluginProcessor::frequencySlopeParameterId,
                            "SLOPE", [] (const double value) { return juce::String(value, 1); }),
      correlationBlockSizeControl(processorRef.getParameters(), PluginProcessor::correlationBlockSizeParameterId,
                                  "BLOCK-SIZE", formatBlockSizeChoice),
      correlationOverlapControl(processorRef.getParameters(), PluginProcessor::correlationOverlapParameterId,
                                "OVERLAP", [] (const double value) { return juce::String(juce::roundToInt(value * 100.0)); }),
      correlationAverageTimeControl(processorRef.getParameters(), PluginProcessor::correlationAverageTimeParameterId,
                                    "AVG-TIME", [] (const double value) { return juce::String(juce::roundToInt(value)); }),
      correlationSmoothingControl(processorRef.getParameters(), PluginProcessor::correlationSmoothingParameterId,
                                  "SMOOTHING", [] (const double value) { return juce::String(juce::roundToInt(value)); }),
      correlationFirstSpectrumTypeControl(processorRef.getParameters(), PluginProcessor::correlationFirstSpectrumTypeParameterId,
                                          "1ST-TYPE", formatSpectrumTypeChoice),
      correlationSecondSpectrumTypeControl(processorRef.getParameters(), PluginProcessor::correlationSecondSpectrumTypeParameterId,
                                           "2ND-TYPE", formatSpectrumTypeChoice),
      levelMeterWidthControl(processorRef.getParameters(), PluginProcessor::levelMeterWidthParameterId,
                             "METER-WIDTH", [] (const double value) { return juce::String(juce::roundToInt(value)); }),
      levelPeakHighControl(processorRef.getParameters(), PluginProcessor::levelPeakHighParameterId,
                            "PEAK/RMS-HIGH", [] (const double value) { return juce::String(value, 1); }),
      levelPeakLowControl(processorRef.getParameters(), PluginProcessor::levelPeakLowParameterId,
                           "PEAK/RMS-LOW", [] (const double value) { return juce::String(value, 1); }),
      levelRmsWindowControl(processorRef.getParameters(), PluginProcessor::levelRmsWindowParameterId,
                            "RMS-WINDOW", [] (const double value) { return juce::String(juce::roundToInt(value)); }),
      levelPeakHoldTimeControl(processorRef.getParameters(), PluginProcessor::levelPeakHoldTimeParameterId,
                               "PEAK-HOLD", [] (const double value) { return juce::String(juce::roundToInt(value)); }),
      levelLufsHighControl(processorRef.getParameters(), PluginProcessor::levelLufsHighParameterId,
                            "LUFS-HIGH", [] (const double value) { return juce::String(value, 1); }),
      levelLufsLowControl(processorRef.getParameters(), PluginProcessor::levelLufsLowParameterId,
                           "LUFS-LOW", [] (const double value) { return juce::String(value, 1); })
{
    setOpaque(true);
    settingsViewport.setViewedComponent(&settingsContent, false);
    settingsViewport.setScrollBarsShown(true, false, true, false);
    settingsViewport.setScrollBarThickness(ana::ui::gap.pixels());
    settingsViewport.setLookAndFeel(&focusedControlLookAndFeel);
    settingsViewport.setWantsKeyboardFocus(false);
    settingsViewport.setMouseClickGrabsKeyboardFocus(false);
    addAndMakeVisible(settingsViewport);

    const auto configureHeading = [this] (EllipsisLabel& label, const juce::String& text)
    {
        label.setText(text, juce::dontSendNotification);
        label.setFont(ana::ui::makeFont());
        label.setJustificationType(juce::Justification::centredLeft);
        label.setColour(juce::Label::textColourId, ana::ui::white);
        label.setDrawBackground(false);
        label.setColour(juce::Label::outlineColourId, ana::ui::grey500);
        label.setBorderSize(juce::BorderSize<int>(1, ana::ui::gap.pixels(), 1, ana::ui::gap.pixels()));
        label.setInterceptsMouseClicks(false, false);
        settingsContent.addAndMakeVisible(label);
    };
    configureHeading(generalHeadingLabel, "CROSSOVER");
    configureHeading(controlsVisibilityHeadingLabel, "VIEW");
    configureHeading(realtimeHeadingLabel, "REALTIME");

    for (auto* component : std::array<juce::Component*, 54> {
             &addCrossoverButton, &removeCrossoverButton, &equalHeightButton,
             &styleControl, &opacityControl, &zoomControlsButton,
             &monitorControlsButton, &otherControlsButton, &timeControl,
             &timeNoteControl, &timeBaseControl, &frequencyBlockSizeControl,
             &frequencyOverlapControl, &frequencyAverageTimeControl, &frequencySmoothingControl,
             &frequencyFilledDisplayButton, &frequencySecondSpectrumButton,
             &frequencyFirstSpectrumTypeControl, &frequencySecondSpectrumTypeControl,
             &frequencyAntiAliasButton, &frequencySlopeControl, &frequencyHostClearButton,
             &frequencyRangesButton, &frequencyCursorButton,
             &frequencyMonitorControlsButton, &frequencyZoomControlsButton,
             &correlationBlockSizeControl, &correlationOverlapControl, &correlationAverageTimeControl,
             &correlationSmoothingControl, &correlationFirstSpectrumTypeControl,
             &correlationSecondSpectrumTypeControl,
             &correlationFilledDisplayButton, &correlationSecondSpectrumButton, &correlationHostClearButton,
             &correlationRangesButton, &correlationCursorButton,
             &correlationZoomControlsButton, &levelMeterWidthControl,
             &levelPeakHighControl, &levelPeakLowControl,
             &levelRmsWindowControl, &levelPeakHoldTimeControl,
             &levelLufsHighControl, &levelLufsLowControl,
             &levelHostResetButton, &centerLevelPartsButton,
             &levelPeakVisibleButton, &levelLoudnessVisibleButton,
             &levelHistoryVisibleButton, &levelHistoryMomentaryButton,
             &levelHistoryShortTermButton, &levelHistoryIntegratedButton,
             &levelHistoryZoomButton })
        settingsContent.addAndMakeVisible(*component);

    addAndMakeVisible(focusedParameterControl);
    addCrossoverButton.onClick = [this] { changeActiveSplitCount(1); };
    removeCrossoverButton.onClick = [this] { changeActiveSplitCount(-1); };
    equalHeightButton.onClick = [this]
    {
        if (onEqualBandHeights)
            onEqualBandHeights();
    };
    centerLevelPartsButton.onClick = [this]
    {
        if (onCenterLevelParts)
            onCenterLevelParts();
    };
    const auto displaySettingChanged = [this]
    {
        if (onDisplaySettingsChanged)
            onDisplaySettingsChanged();
    };
    styleControl.onValueChanged = displaySettingChanged;
    opacityControl.onValueChanged = displaySettingChanged;
    for (auto* button : std::array<ControlButton*, 3> {
             &zoomControlsButton, &monitorControlsButton, &otherControlsButton })
    {
        button->setClickingTogglesState(true);
        button->onClick = displaySettingChanged;
    }
    zoomControlsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), PluginProcessor::scopeZoomControlsParameterId, zoomControlsButton);
    monitorControlsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), PluginProcessor::scopeMonitorControlsParameterId, monitorControlsButton);
    otherControlsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), PluginProcessor::scopeOtherControlsParameterId, otherControlsButton);
    for (auto* button : std::array<ControlButton*, 8> {
             &frequencyFilledDisplayButton, &frequencySecondSpectrumButton,
             &frequencyAntiAliasButton, &frequencyHostClearButton, &frequencyRangesButton,
             &frequencyCursorButton, &frequencyMonitorControlsButton, &frequencyZoomControlsButton })
    {
        button->setClickingTogglesState(true);
        button->onClick = displaySettingChanged;
    }
    frequencyFilledDisplayAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), PluginProcessor::frequencyFilledDisplayParameterId, frequencyFilledDisplayButton);
    frequencySecondSpectrumAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), PluginProcessor::frequencySecondSpectrumParameterId, frequencySecondSpectrumButton);
    frequencyAntiAliasAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), PluginProcessor::frequencyAntiAliasParameterId, frequencyAntiAliasButton);
    frequencyRangesAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), PluginProcessor::frequencyRangesVisibleParameterId, frequencyRangesButton);
    frequencyHostClearAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), PluginProcessor::frequencyHostClearParameterId, frequencyHostClearButton);
    frequencyCursorAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), PluginProcessor::frequencyCursorReadoutParameterId, frequencyCursorButton);
    frequencyMonitorControlsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), PluginProcessor::frequencyMonitorControlsParameterId, frequencyMonitorControlsButton);
    frequencyZoomControlsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), PluginProcessor::frequencyZoomControlsParameterId, frequencyZoomControlsButton);
    for (auto* button : std::array<ControlButton*, 6> {
        &correlationFilledDisplayButton, &correlationHostClearButton, &correlationRangesButton,
        &correlationCursorButton, &correlationZoomControlsButton,
        &correlationSecondSpectrumButton })
    {
        button->setClickingTogglesState(true);
        button->onClick = displaySettingChanged;
    }
    correlationFilledDisplayAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), PluginProcessor::correlationFilledDisplayParameterId, correlationFilledDisplayButton);
    correlationSecondSpectrumAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), PluginProcessor::correlationSecondSpectrumParameterId, correlationSecondSpectrumButton);
    correlationHostClearAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), PluginProcessor::correlationHostClearParameterId, correlationHostClearButton);
    correlationRangesAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), PluginProcessor::correlationRangesVisibleParameterId, correlationRangesButton);
    correlationCursorAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), PluginProcessor::correlationCursorReadoutParameterId, correlationCursorButton);
    correlationZoomControlsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), PluginProcessor::correlationZoomControlsParameterId, correlationZoomControlsButton);
    levelHostResetButton.setClickingTogglesState(true);
    levelHostResetButton.onClick = displaySettingChanged;
    levelHostResetAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), PluginProcessor::levelHostResetParameterId, levelHostResetButton);
    for (const auto& [button, parameterId] : std::array {
             std::pair { &levelPeakVisibleButton, PluginProcessor::levelPeakVisibleParameterId },
             std::pair { &levelLoudnessVisibleButton, PluginProcessor::levelLoudnessVisibleParameterId },
             std::pair { &levelHistoryVisibleButton, PluginProcessor::levelHistoryVisibleParameterId } })
    {
        button->setClickingTogglesState(true);
        button->onClick = [this, displaySettingChanged, button, parameterId]
        {
            const auto anyVisible = levelPeakVisibleButton.getToggleState()
                || levelLoudnessVisibleButton.getToggleState()
                || levelHistoryVisibleButton.getToggleState();
            if (! anyVisible)
            {
                if (auto* parameter = processor.getParameters().getParameter(parameterId))
                    parameter->setValueNotifyingHost(1.0f);
                button->setToggleState(true, juce::dontSendNotification);
            }
            processor.clearLevelMeter();
            displaySettingChanged();
        };
    }
    levelPeakVisibleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), PluginProcessor::levelPeakVisibleParameterId, levelPeakVisibleButton);
    levelLoudnessVisibleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), PluginProcessor::levelLoudnessVisibleParameterId,
        levelLoudnessVisibleButton);
    levelHistoryVisibleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), PluginProcessor::levelHistoryVisibleParameterId,
        levelHistoryVisibleButton);
    for (const auto& [button, parameterId] : std::array {
             std::pair { &levelHistoryMomentaryButton, PluginProcessor::levelHistoryMomentaryParameterId },
             std::pair { &levelHistoryShortTermButton, PluginProcessor::levelHistoryShortTermParameterId },
             std::pair { &levelHistoryIntegratedButton, PluginProcessor::levelHistoryIntegratedParameterId } })
    {
        button->setClickingTogglesState(true);
        button->onClick = [this, displaySettingChanged, button, parameterId]
        {
            const auto anyVisible = levelHistoryMomentaryButton.getToggleState()
                || levelHistoryShortTermButton.getToggleState()
                || levelHistoryIntegratedButton.getToggleState();
            if (! anyVisible)
            {
                if (auto* parameter = processor.getParameters().getParameter(parameterId))
                    parameter->setValueNotifyingHost(1.0f);
                button->setToggleState(true, juce::dontSendNotification);
            }
            displaySettingChanged();
        };
    }
    levelHistoryMomentaryAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), PluginProcessor::levelHistoryMomentaryParameterId,
        levelHistoryMomentaryButton);
    levelHistoryShortTermAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), PluginProcessor::levelHistoryShortTermParameterId,
        levelHistoryShortTermButton);
    levelHistoryIntegratedAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), PluginProcessor::levelHistoryIntegratedParameterId,
        levelHistoryIntegratedButton);
    levelHistoryZoomButton.setClickingTogglesState(true);
    levelHistoryZoomButton.onClick = displaySettingChanged;
    levelHistoryZoomAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), PluginProcessor::levelHistoryZoomParameterId,
        levelHistoryZoomButton);
    const auto requestChoice = [this] (ParameterControl& control)
    {
        if (onChoiceRequested)
            onChoiceRequested(control);
    };
    styleControl.onChoiceRequested = requestChoice;
    timeNoteControl.onChoiceRequested = requestChoice;
    timeBaseControl.onChoiceRequested = requestChoice;
    frequencyBlockSizeControl.onChoiceRequested = requestChoice;
    frequencyFirstSpectrumTypeControl.onChoiceRequested = requestChoice;
    frequencySecondSpectrumTypeControl.onChoiceRequested = requestChoice;
    correlationBlockSizeControl.onChoiceRequested = requestChoice;
    correlationFirstSpectrumTypeControl.onChoiceRequested = requestChoice;
    correlationSecondSpectrumTypeControl.onChoiceRequested = requestChoice;

    const auto focusControl = [this] (ParameterControl& control)
    {
        focusParameterControl(control);
    };
    opacityControl.onFocusRequested = focusControl;
    timeControl.onFocusRequested = focusControl;
    frequencyAverageTimeControl.onFocusRequested = focusControl;
    frequencySmoothingControl.onFocusRequested = focusControl;
    frequencySlopeControl.onFocusRequested = focusControl;
    frequencyOverlapControl.onFocusRequested = focusControl;
    correlationOverlapControl.onFocusRequested = focusControl;
    correlationAverageTimeControl.onFocusRequested = focusControl;
    correlationSmoothingControl.onFocusRequested = focusControl;
    levelMeterWidthControl.onFocusRequested = focusControl;
    levelPeakHighControl.onFocusRequested = focusControl;
    levelPeakLowControl.onFocusRequested = focusControl;
    levelRmsWindowControl.onFocusRequested = focusControl;
    levelPeakHoldTimeControl.onFocusRequested = focusControl;
    levelLufsHighControl.onFocusRequested = focusControl;
    levelLufsLowControl.onFocusRequested = focusControl;
    levelMeterWidthControl.onValueChanged = displaySettingChanged;
    levelPeakHighControl.onValueChanged = displaySettingChanged;
    levelPeakLowControl.onValueChanged = displaySettingChanged;
    levelRmsWindowControl.onValueChanged = displaySettingChanged;
    levelPeakHoldTimeControl.onValueChanged = displaySettingChanged;
    levelLufsHighControl.onValueChanged = displaySettingChanged;
    levelLufsLowControl.onValueChanged = displaySettingChanged;
    frequencySmoothingControl.onValueChanged = displaySettingChanged;
    correlationSmoothingControl.onValueChanged = displaySettingChanged;
    frequencyOverlapControl.getSlider().valueFromTextFunction = [] (const juce::String& text)
    {
        return text.getDoubleValue() * 0.01;
    };
    correlationOverlapControl.getSlider().valueFromTextFunction = [] (const juce::String& text)
    {
        return text.getDoubleValue() * 0.01;
    };
    timeBaseControl.onValueChanged = [this]
    {
        refreshExternalState();
        resized();

        if (onDisplaySettingsChanged)
            onDisplaySettingsChanged();
    };
    timeNoteControl.onValueChanged = displaySettingChanged;
    timeControl.getSlider().valueFromTextFunction = [] (const juce::String& text)
    {
        return text.getDoubleValue() * 1000.0;
    };

    focusedParameterControl.setLookAndFeel(&focusedControlLookAndFeel);
    focusedParameterControl.setSliderStyle(juce::Slider::LinearHorizontal);
    focusedParameterControl.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    focusedParameterControl.setRange(0.0, 1.0, 0.0);
    focusedParameterControl.setSliderSnapsToMousePosition(false);
    focusedParameterControl.setScrollWheelEnabled(true);
    focusedParameterControl.setWantsKeyboardFocus(false);
    focusedParameterControl.setMouseClickGrabsKeyboardFocus(false);
    focusedParameterControl.setEnabled(false);
    focusedParameterControl.onValueChange = [this]
    {
        if (updatingFocusedParameterControl || focusedParameterTarget == nullptr)
            return;

        focusedParameterTarget->commitPendingEditor();
        auto& target = focusedParameterTarget->getSlider();
        const auto value = target.getNormalisableRange().convertFrom0to1(focusedParameterControl.getValue());
        target.setValue(value, juce::sendNotificationSync);
        syncFocusedParameterControl();
    };

    for (size_t index = 0; index < crossoverControls.size(); ++index)
    {
        auto control = std::make_unique<ParameterControl>(
            processor.getParameters(),
            PluginProcessor::crossoverParameterIds[index],
            "XOVER-" + juce::String(static_cast<int>(index + 1)),
            [] (const double value) { return formatFrequency(value); });
        control->getSlider().valueFromTextFunction = [] (const juce::String& text)
        {
            return parseFrequency(text);
        };
        control->onValueChanged = [this, index] { constrainFrequency(index); };
        control->onFocusRequested = focusControl;
        settingsContent.addAndMakeVisible(*control);
        crossoverControls[index] = std::move(control);
    }

    for (size_t index = 0; index < processor.getActiveSplitCount(); ++index)
        constrainFrequency(index);

    refreshExternalState();
    focusParameterControl(opacityControl);
    settingsContent.addMouseListener(this, true);
    startTimerHz(15);
}

SettingsPanel::~SettingsPanel()
{
    settingsContent.removeMouseListener(this);
    settingsViewport.setViewedComponent(nullptr, false);
    settingsViewport.setLookAndFeel(nullptr);
    focusedParameterControl.setLookAndFeel(nullptr);
}

void SettingsPanel::setAnalyzerPage(const AnalyzerPage page)
{
    if (analyzerPage == page)
        return;

    analyzerPage = page;
    generalHeadingLabel.setText(page == AnalyzerPage::scope ? "CROSSOVER" : "MAIN",
                                juce::dontSendNotification);
    clearFocusedParameterControl();
    refreshExternalState();
    resized();
}

void SettingsPanel::paint(juce::Graphics& graphics)
{
    graphics.fillAll(ana::ui::black);
}

void SettingsPanel::resized()
{
    constexpr int headingHeight = ana::ui::controlHeight;
    constexpr int rowHeight = ana::ui::controlHeight;
    const auto& fixedGap = ana::ui::gap;
    const auto scopePage = analyzerPage == AnalyzerPage::scope;
    const auto frequencyPage = analyzerPage == AnalyzerPage::frequency;
    const auto levelPage = analyzerPage == AnalyzerPage::level;
    const auto contentHeight = scopePage
        ? 17 * rowHeight + 16 * fixedGap.pixels()
        : frequencyPage ? 17 * rowHeight + 16 * fixedGap.pixels()
        : levelPage ? 16 * rowHeight + 15 * fixedGap.pixels() : 14 * rowHeight + 13 * fixedGap.pixels();
    auto innerBounds = getLocalBounds().reduced(fixedGap.pixels());
    const auto potentiometerBounds = innerBounds.removeFromBottom(rowHeight);
    fixedGap.removeFromBottom(innerBounds);
    const auto viewportBounds = innerBounds;
    settingsViewport.setBounds(viewportBounds);
    const auto needsScrollbar = contentHeight > viewportBounds.getHeight();
    const auto scrollbarReserve = needsScrollbar
        ? settingsViewport.getScrollBarThickness() + fixedGap.pixels()
        : 0;
    const auto contentWidth = std::max(1, viewportBounds.getWidth()
        - scrollbarReserve);
    settingsContent.setSize(contentWidth, std::max(contentHeight, viewportBounds.getHeight()));
    focusedParameterControl.setBounds(viewportBounds.getX(), potentiometerBounds.getY(),
                                      viewportBounds.getWidth(), rowHeight);
    auto area = settingsContent.getLocalBounds();
    const auto placeButton = [&] (ControlButton& button)
    {
        button.setBounds(area.removeFromTop(rowHeight));
    };

    generalHeadingLabel.setBounds(area.removeFromTop(headingHeight));
    fixedGap.removeFromTop(area);

    if (levelPage)
    {
        levelRmsWindowControl.setBounds(area.removeFromTop(rowHeight));
        fixedGap.removeFromTop(area);
        levelPeakHoldTimeControl.setBounds(area.removeFromTop(rowHeight));
        fixedGap.removeFromTop(area);
        placeButton(levelHostResetButton);
        fixedGap.removeFromTop(area);
        controlsVisibilityHeadingLabel.setBounds(area.removeFromTop(headingHeight));
        fixedGap.removeFromTop(area);
        levelMeterWidthControl.setBounds(area.removeFromTop(rowHeight));
        fixedGap.removeFromTop(area);
        placeButton(centerLevelPartsButton);
        fixedGap.removeFromTop(area);
        levelPeakHighControl.setBounds(area.removeFromTop(rowHeight));
        fixedGap.removeFromTop(area);
        levelPeakLowControl.setBounds(area.removeFromTop(rowHeight));
        fixedGap.removeFromTop(area);
        levelLufsHighControl.setBounds(area.removeFromTop(rowHeight));
        fixedGap.removeFromTop(area);
        levelLufsLowControl.setBounds(area.removeFromTop(rowHeight));
        fixedGap.removeFromTop(area);
        placeButton(levelPeakVisibleButton);
        fixedGap.removeFromTop(area);
        placeButton(levelLoudnessVisibleButton);
        fixedGap.removeFromTop(area);
        placeButton(levelHistoryVisibleButton);
        fixedGap.removeFromTop(area);
        auto historyButtons = area.removeFromTop(rowHeight);
        const auto historyButtonWidth = std::max(0,
            (historyButtons.getWidth() - fixedGap.pixels() * 2) / 3);
        levelHistoryMomentaryButton.setBounds(historyButtons.removeFromLeft(historyButtonWidth));
        fixedGap.removeFromLeft(historyButtons);
        levelHistoryShortTermButton.setBounds(historyButtons.removeFromLeft(historyButtonWidth));
        fixedGap.removeFromLeft(historyButtons);
        levelHistoryIntegratedButton.setBounds(historyButtons);
        fixedGap.removeFromTop(area);
        placeButton(levelHistoryZoomButton);
        return;
    }

    if (frequencyPage)
    {
        frequencyBlockSizeControl.setBounds(area.removeFromTop(rowHeight));
        fixedGap.removeFromTop(area);
        frequencyOverlapControl.setBounds(area.removeFromTop(rowHeight));
        fixedGap.removeFromTop(area);
        frequencyAverageTimeControl.setBounds(area.removeFromTop(rowHeight));
        fixedGap.removeFromTop(area);
        frequencySmoothingControl.setBounds(area.removeFromTop(rowHeight));
        fixedGap.removeFromTop(area);
        controlsVisibilityHeadingLabel.setBounds(area.removeFromTop(headingHeight));
        fixedGap.removeFromTop(area);
        placeButton(frequencyFilledDisplayButton);
        fixedGap.removeFromTop(area);
        placeButton(frequencySecondSpectrumButton);
        fixedGap.removeFromTop(area);
        frequencyFirstSpectrumTypeControl.setBounds(area.removeFromTop(rowHeight));
        fixedGap.removeFromTop(area);
        frequencySecondSpectrumTypeControl.setBounds(area.removeFromTop(rowHeight));
        fixedGap.removeFromTop(area);
        placeButton(frequencyAntiAliasButton);
        fixedGap.removeFromTop(area);
        frequencySlopeControl.setBounds(area.removeFromTop(rowHeight));
        fixedGap.removeFromTop(area);
        placeButton(frequencyHostClearButton);
        fixedGap.removeFromTop(area);
        placeButton(frequencyRangesButton);
        fixedGap.removeFromTop(area);
        placeButton(frequencyCursorButton);
        fixedGap.removeFromTop(area);
        placeButton(frequencyMonitorControlsButton);
        fixedGap.removeFromTop(area);
        placeButton(frequencyZoomControlsButton);
        return;
    }

    if (! scopePage)
    {
        correlationBlockSizeControl.setBounds(area.removeFromTop(rowHeight));
        fixedGap.removeFromTop(area);
        correlationOverlapControl.setBounds(area.removeFromTop(rowHeight));
        fixedGap.removeFromTop(area);
        correlationAverageTimeControl.setBounds(area.removeFromTop(rowHeight));
        fixedGap.removeFromTop(area);
        correlationSmoothingControl.setBounds(area.removeFromTop(rowHeight));
        fixedGap.removeFromTop(area);
        controlsVisibilityHeadingLabel.setBounds(area.removeFromTop(headingHeight));
        fixedGap.removeFromTop(area);
        placeButton(correlationFilledDisplayButton);
        fixedGap.removeFromTop(area);
        placeButton(correlationSecondSpectrumButton);
        fixedGap.removeFromTop(area);
        correlationFirstSpectrumTypeControl.setBounds(area.removeFromTop(rowHeight));
        fixedGap.removeFromTop(area);
        correlationSecondSpectrumTypeControl.setBounds(area.removeFromTop(rowHeight));
        fixedGap.removeFromTop(area);
        placeButton(correlationHostClearButton);
        fixedGap.removeFromTop(area);
        placeButton(correlationRangesButton);
        fixedGap.removeFromTop(area);
        placeButton(correlationCursorButton);
        fixedGap.removeFromTop(area);
        placeButton(correlationZoomControlsButton);
        return;
    }

    auto crossoverButtons = area.removeFromTop(rowHeight);
    const auto crossoverButtonWidth = std::max(0,
        (crossoverButtons.getWidth() - fixedGap.pixels()) / 2);
    addCrossoverButton.setBounds(crossoverButtons.removeFromLeft(crossoverButtonWidth));
    fixedGap.removeFromLeft(crossoverButtons);
    removeCrossoverButton.setBounds(crossoverButtons);
    fixedGap.removeFromTop(area);

    for (auto& control : crossoverControls)
    {
        control->setBounds(area.removeFromTop(rowHeight));
        fixedGap.removeFromTop(area);
    }

    realtimeHeadingLabel.setBounds(area.removeFromTop(headingHeight));
    fixedGap.removeFromTop(area);
    const auto timeBounds = area.removeFromTop(rowHeight);
    timeControl.setBounds(timeBounds);
    timeNoteControl.setBounds(timeBounds);
    fixedGap.removeFromTop(area);
    timeBaseControl.setBounds(area.removeFromTop(rowHeight));
    fixedGap.removeFromTop(area);

    controlsVisibilityHeadingLabel.setBounds(area.removeFromTop(headingHeight));
    fixedGap.removeFromTop(area);
    placeButton(equalHeightButton);
    fixedGap.removeFromTop(area);
    styleControl.setBounds(area.removeFromTop(rowHeight));
    fixedGap.removeFromTop(area);
    opacityControl.setBounds(area.removeFromTop(rowHeight));
    fixedGap.removeFromTop(area);
    placeButton(zoomControlsButton);
    fixedGap.removeFromTop(area);
    placeButton(monitorControlsButton);
    fixedGap.removeFromTop(area);
    placeButton(otherControlsButton);
}

void SettingsPanel::mouseDown(const juce::MouseEvent& event)
{
    if (event.originalComponent != &settingsContent && event.originalComponent != this)
        return;

    dismissParameterEditors();
    clearFocusedParameterControl();
}

void SettingsPanel::timerCallback()
{
    refreshExternalState();
    syncFocusedParameterControl();
}

void SettingsPanel::changeActiveSplitCount(const int delta)
{
    const auto currentCount = static_cast<int>(processor.getActiveSplitCount());
    const auto newCount = juce::jlimit(0, static_cast<int>(crossoverControls.size()), currentCount + delta);

    if (newCount != currentCount)
    {
        processor.setActiveSplitCount(static_cast<size_t>(newCount));

        for (int index = 0; index < newCount; ++index)
            constrainFrequency(static_cast<size_t>(index));
    }

    refreshExternalState();
}

void SettingsPanel::constrainFrequency(const size_t crossoverIndex)
{
    if (constrainingFrequency || crossoverIndex >= processor.getActiveSplitCount())
        return;

    const juce::ScopedValueSetter<bool> guard(constrainingFrequency, true);
    const auto activeSplitCount = processor.getActiveSplitCount();
    auto& slider = crossoverControls[crossoverIndex]->getSlider();
    auto lowerBound = slider.getMinimum();
    auto upperBound = slider.getMaximum();

    if (crossoverIndex > 0)
        lowerBound = crossoverControls[crossoverIndex - 1]->getSlider().getValue() + minimumCrossoverGapHz;

    if (crossoverIndex + 1 < activeSplitCount)
        upperBound = crossoverControls[crossoverIndex + 1]->getSlider().getValue() - minimumCrossoverGapHz;

    slider.setValue(juce::jlimit(lowerBound, std::max(lowerBound, upperBound), slider.getValue()),
                    juce::sendNotificationSync);
}

void SettingsPanel::refreshExternalState()
{
    const auto scopePage = analyzerPage == AnalyzerPage::scope;
    const auto frequencyPage = analyzerPage == AnalyzerPage::frequency;
    const auto correlationPage = analyzerPage == AnalyzerPage::correlation;
    const auto levelPage = analyzerPage == AnalyzerPage::level;
    controlsVisibilityHeadingLabel.setVisible(scopePage || frequencyPage || correlationPage || levelPage);
    for (auto* component : std::array<juce::Component*, 11> {
             &addCrossoverButton, &removeCrossoverButton, &equalHeightButton,
             &styleControl, &opacityControl, &zoomControlsButton,
             &monitorControlsButton, &otherControlsButton, &timeControl,
             &timeNoteControl, &timeBaseControl })
        component->setVisible(scopePage);
    for (auto& control : crossoverControls)
        control->setVisible(scopePage);
    realtimeHeadingLabel.setVisible(scopePage);

    for (auto* component : std::array<juce::Component*, 15> {
             &frequencyBlockSizeControl, &frequencyOverlapControl, &frequencyAverageTimeControl,
             &frequencySmoothingControl,
             &frequencyFilledDisplayButton, &frequencySecondSpectrumButton,
             &frequencyFirstSpectrumTypeControl, &frequencySecondSpectrumTypeControl,
             &frequencyAntiAliasButton, &frequencySlopeControl, &frequencyHostClearButton,
             &frequencyRangesButton, &frequencyCursorButton,
             &frequencyMonitorControlsButton, &frequencyZoomControlsButton })
        component->setVisible(frequencyPage);

    for (auto* component : std::array<juce::Component*, 12> {
             &correlationBlockSizeControl, &correlationOverlapControl, &correlationAverageTimeControl,
             &correlationSmoothingControl, &correlationFirstSpectrumTypeControl,
             &correlationSecondSpectrumTypeControl,
             &correlationFilledDisplayButton, &correlationSecondSpectrumButton,
             &correlationHostClearButton, &correlationRangesButton,
             &correlationCursorButton, &correlationZoomControlsButton })
        component->setVisible(correlationPage);

    for (auto* component : std::array<juce::Component*, 15> {
             &levelMeterWidthControl, &levelPeakHighControl, &levelPeakLowControl,
             &levelRmsWindowControl, &levelPeakHoldTimeControl,
             &levelLufsHighControl, &levelLufsLowControl,
             &levelHostResetButton, &levelPeakVisibleButton,
             &levelLoudnessVisibleButton, &levelHistoryVisibleButton,
             &levelHistoryMomentaryButton, &levelHistoryShortTermButton,
             &levelHistoryIntegratedButton, &levelHistoryZoomButton })
        component->setVisible(levelPage);
    centerLevelPartsButton.setVisible(levelPage);

    if (! scopePage)
        return;

    const auto activeSplitCount = processor.getActiveSplitCount();
    addCrossoverButton.setEnabled(activeSplitCount < crossoverControls.size());
    removeCrossoverButton.setEnabled(activeSplitCount > 0);

    for (size_t index = 0; index < crossoverControls.size(); ++index)
        crossoverControls[index]->setInteractionEnabled(index < activeSplitCount);

    const auto noteTime = processor.isScopeTimeNoteBased();
    timeControl.setVisible(! noteTime);
    timeNoteControl.setVisible(noteTime);

    if (noteTime && focusedParameterTarget == &timeControl)
        clearFocusedParameterControl();

    if (focusedParameterTarget != nullptr && ! focusedParameterTarget->isInteractionEnabled())
    {
        focusedParameterTarget->setSelected(false);
        focusedParameterTarget = nullptr;
        focusedParameterControl.setEnabled(false);
    }
}

void SettingsPanel::focusParameterControl(ParameterControl& control)
{
    if (! control.isInteractionEnabled() || ! control.supportsFocusedPotentiometer())
        return;

    if (focusedParameterTarget != nullptr)
        focusedParameterTarget->setSelected(false);

    focusedParameterTarget = &control;
    focusedParameterTarget->setSelected(true);

    focusedParameterControl.setEnabled(true);
    syncFocusedParameterControl();
}

void SettingsPanel::clearFocusedParameterControl()
{
    if (focusedParameterTarget != nullptr)
        focusedParameterTarget->setSelected(false);

    focusedParameterTarget = nullptr;
    focusedParameterControl.setEnabled(false);
}

void SettingsPanel::dismissParameterEditors()
{
    styleControl.commitPendingEditor();
    opacityControl.commitPendingEditor();
    timeControl.commitPendingEditor();
    timeNoteControl.commitPendingEditor();
    timeBaseControl.commitPendingEditor();
    frequencyBlockSizeControl.commitPendingEditor();
    frequencyOverlapControl.commitPendingEditor();
    frequencyAverageTimeControl.commitPendingEditor();
    frequencySmoothingControl.commitPendingEditor();
    frequencyFirstSpectrumTypeControl.commitPendingEditor();
    frequencySecondSpectrumTypeControl.commitPendingEditor();
    frequencySlopeControl.commitPendingEditor();
    correlationBlockSizeControl.commitPendingEditor();
    correlationOverlapControl.commitPendingEditor();
    correlationAverageTimeControl.commitPendingEditor();
    correlationSmoothingControl.commitPendingEditor();
    levelMeterWidthControl.commitPendingEditor();
    levelPeakHighControl.commitPendingEditor();
    levelPeakLowControl.commitPendingEditor();
    levelRmsWindowControl.commitPendingEditor();
    levelPeakHoldTimeControl.commitPendingEditor();
    levelLufsHighControl.commitPendingEditor();
    levelLufsLowControl.commitPendingEditor();

    for (auto& control : crossoverControls)
        control->commitPendingEditor();
}

void SettingsPanel::syncFocusedParameterControl()
{
    if (focusedParameterTarget == nullptr
        || ! focusedParameterTarget->isInteractionEnabled()
        || ! focusedParameterTarget->supportsFocusedPotentiometer())
        return;

    const auto& target = focusedParameterTarget->getSlider();
    const auto normalisedValue = target.getNormalisableRange().convertTo0to1(target.getValue());

    if (std::abs(focusedParameterControl.getValue() - normalisedValue) > 1.0e-6)
    {
        const juce::ScopedValueSetter<bool> guard(updatingFocusedParameterControl, true);
        focusedParameterControl.setValue(normalisedValue, juce::dontSendNotification);
    }
}

PluginEditor::PluginEditor(PluginProcessor& processorRef)
    : AudioProcessorEditor(&processorRef)
#if JucePlugin_Enable_ARA
    , AudioProcessorEditorARAExtension(&processorRef)
#endif
    , audioProcessor(processorRef),
      scopeDisplay(processorRef),
      settingsComponent(processorRef),
      frequencyDisplay(processorRef),
      correlationDisplay(processorRef),
      levelDisplay(processorRef)
{
    for (auto* component : std::array<juce::Component*, 21> {
             &scopeDisplay, &settingsComponent, &frequencyDisplay, &correlationDisplay, &levelDisplay,
             &offlineUpdateLabel, &frequencyButton, &correlationButton,
             &levelButton, &scopeButton, &settingsButton, &realtimeButton, &offlineButton, &sourceButton,
             &takeButton, &refreshButton, &fullSourceButton, &clearButton, &freezeButton,
             &controlsButton, &mixolveButton })
        addAndMakeVisible(*component);

    frequencyButton.onClick = [this] { showAnalyzerPage(AnalyzerPage::frequency, showingAnalyzerSettings); };
    correlationButton.onClick = [this] { showAnalyzerPage(AnalyzerPage::correlation, showingAnalyzerSettings); };
    levelButton.onClick = [this] { showAnalyzerPage(AnalyzerPage::level, showingAnalyzerSettings); };
    scopeButton.onClick = [this] { showAnalyzerPage(AnalyzerPage::scope, showingAnalyzerSettings); };
    settingsButton.onClick = [this] { showAnalyzerSettings(! showingAnalyzerSettings); };
    controlsButton.setClickingTogglesState(true);
    controlsButton.setToggleState(false, juce::dontSendNotification);
    controlsButton.onClick = [this]
    {
        mainControlsVisible = controlsButton.getToggleState();
        resized();
    };
    controlsButton.setTooltip("CONTROLS");
    mixolveButton.setTooltip("MIXOLVE");
    settingsButton.setTooltip("SETTINGS");
    freezeButton.setTooltip("FREEZE");
    refreshButton.setTooltip("REFRESH");
    mixolveButton.onClick = [this] { showAboutPopup(); };

    offlineUpdateLabel.setFont(ana::ui::makeFont());
    offlineUpdateLabel.setJustificationType(juce::Justification::centred);
    offlineUpdateLabel.setMinimumHorizontalScale(1.0f);
    offlineUpdateLabel.setColour(juce::Label::textColourId, ana::ui::white);
    offlineUpdateLabel.setColour(juce::Label::backgroundColourId, ana::ui::field);
    offlineUpdateLabel.setColour(juce::Label::outlineColourId, ana::ui::grey500);
    offlineUpdateLabel.setBorderSize(
        juce::BorderSize<int>(1, ana::ui::gap.pixels(), 1, ana::ui::gap.pixels()));
    offlineUpdateLabel.setInterceptsMouseClicks(false, false);
    offlineUpdateLabel.setVisible(true);
    scopeDisplay.onOfflineUpdateStatus = [this] (const juce::String& status)
    {
        offlineUpdateLabel.setText(status, juce::dontSendNotification);
    };
    correlationDisplay.onOfflineUpdateStatus = [this] (const juce::String& status)
    {
        offlineUpdateLabel.setText(status, juce::dontSendNotification);
    };
    settingsComponent.onDisplaySettingsChanged = [this]
    {
        scopeDisplay.refreshDisplaySettings();
        frequencyDisplay.resized();
        frequencyDisplay.repaint();
        correlationDisplay.resized();
        correlationDisplay.repaint();
        levelDisplay.resized();
        if (audioProcessor.isOfflineMode())
            scopeDisplay.refreshWaveform();
    };
    settingsComponent.onEqualBandHeights = [this] { scopeDisplay.equalizeBandHeights(); };
    settingsComponent.onCenterLevelParts = [this] { levelDisplay.centerParts(); };
    settingsComponent.onChoiceRequested = [this] (ParameterControl& control)
    {
        showChoicePrompt(control);
    };

    freezeButton.setClickingTogglesState(true);
    freezeButton.onClick = [this]
    {
        if (activePage == AnalyzerPage::frequency)
            audioProcessor.getFrequencySpectrum().setFrozen(freezeButton.getToggleState());
        else if (activePage == AnalyzerPage::correlation)
            audioProcessor.getCorrelationSpectrum().setFrozen(freezeButton.getToggleState());
        else if (activePage == AnalyzerPage::level)
            audioProcessor.getLevelMeter().setFrozen(freezeButton.getToggleState());
        else
        {
            scopeFrozen = freezeButton.getToggleState();
            scopeDisplay.setFrozen(scopeFrozen);
        }
    };
    refreshButton.onClick = [this] { scopeDisplay.refreshWaveform(); };
    fullSourceButton.onClick = [this]
    {
        const auto enabled = ! audioProcessor.isScopeFullSourceView();
        audioProcessor.setScopeFullSourceView(enabled);
        scopeDisplay.setFullSourceView(enabled);
    };
    clearButton.onClick = [this]
    {
        if (activePage == AnalyzerPage::frequency)
            audioProcessor.clearFrequencySpectrum();
        else if (activePage == AnalyzerPage::correlation)
            audioProcessor.clearCorrelationSpectrum();
        else if (activePage == AnalyzerPage::level)
            audioProcessor.clearLevelMeter();
        else
            scopeDisplay.clearHistory();
    };
    realtimeButton.onClick = [this] { audioProcessor.setOfflineMode(false); };
    realtimeButton.setTooltip("Realtime input mode");
    offlineButton.onClick = [this] { audioProcessor.setOfflineMode(true); };
    offlineButton.setTooltip("ARA offline source mode");
    sourceButton.onClick = [this] { showOfflineSourcePrompt(); };
    takeButton.onClick = [this] { showOfflineTakePrompt(); };

    showAnalyzerPage(static_cast<AnalyzerPage>(audioProcessor.getAnalyzerPageState()),
                     false);
    timerCallback();
    startTimerHz(15);
    setResizable(true, false);
    setResizeLimits(minimumEditorWidth, minimumEditorHeight, maximumEditorSize, maximumEditorSize);
    rightEdgeResizer = std::make_unique<InvisibleResizableEdgeComponent>(
        this, getConstrainer(), juce::ResizableEdgeComponent::rightEdge);
    bottomEdgeResizer = std::make_unique<InvisibleResizableEdgeComponent>(
        this, getConstrainer(), juce::ResizableEdgeComponent::bottomEdge);
    addAndMakeVisible(*rightEdgeResizer);
    addAndMakeVisible(*bottomEdgeResizer);
    const auto savedSize = audioProcessor.getLastEditorSize();
    setSize(juce::jlimit(minimumEditorWidth, maximumEditorSize,
                         savedSize.x > 0 ? savedSize.x : defaultEditorWidth),
            juce::jlimit(minimumEditorHeight, maximumEditorSize,
                         savedSize.y > 0 ? savedSize.y : defaultEditorHeight));
}

PluginEditor::~PluginEditor()
{
    if (getWidth() > 0 && getHeight() > 0)
        audioProcessor.setLastEditorSize(getWidth(), getHeight());
}

void PluginEditor::showAnalyzerSettings(const bool shouldShowSettings)
{
    showAnalyzerPage(activePage, shouldShowSettings);
}

void PluginEditor::showAnalyzerPage(const AnalyzerPage page,
                                               const bool shouldShowSettings)
{
    if (! shouldShowSettings)
        dismissChoicePrompt();

    activePage = page;
    audioProcessor.setAnalyzerPageState(static_cast<int>(page));
    scopeButton.setToggleState(page == AnalyzerPage::scope, juce::dontSendNotification);
    frequencyButton.setToggleState(page == AnalyzerPage::frequency, juce::dontSendNotification);
    correlationButton.setToggleState(page == AnalyzerPage::correlation, juce::dontSendNotification);
    levelButton.setToggleState(page == AnalyzerPage::level, juce::dontSendNotification);
    showingAnalyzerSettings = shouldShowSettings;
    settingsButton.setEnabled(true);
    settingsButton.setToggleState(showingAnalyzerSettings, juce::dontSendNotification);
    scopeDisplay.setVisible(page == AnalyzerPage::scope);
    frequencyDisplay.setVisible(page == AnalyzerPage::frequency);
    correlationDisplay.setVisible(page == AnalyzerPage::correlation);
    levelDisplay.setVisible(page == AnalyzerPage::level);
    fullSourceButton.setEnabled(page == AnalyzerPage::scope);
    freezeButton.setToggleState(page == AnalyzerPage::frequency
                                    ? audioProcessor.getFrequencySpectrum().isFrozen()
                                    : page == AnalyzerPage::correlation
                                        ? audioProcessor.getCorrelationSpectrum().isFrozen()
                                    : page == AnalyzerPage::level
                                        ? audioProcessor.getLevelMeter().isFrozen()
                                    : scopeFrozen,
                                juce::dontSendNotification);
    settingsComponent.setAnalyzerPage(page);
    settingsComponent.setVisible(showingAnalyzerSettings);

    if (showingAnalyzerSettings)
        settingsComponent.toFront(false);

    resized();
    repaint();
}

void PluginEditor::showChoicePrompt(ParameterControl& control)
{
    const auto choices = control.getChoiceNames();

    if (choices.isEmpty())
        return;

    const auto anchorBounds = getLocalArea(&control, control.getValueBounds());
    juce::Component::SafePointer<ParameterControl> safeControl(&control);
    showChoicePrompt(anchorBounds, choices, control.getSelectedChoiceIndex(),
        [safeControl] (const int selectedIndex)
        {
            if (safeControl != nullptr)
                safeControl->setSelectedChoiceIndex(selectedIndex);
        });
}

void PluginEditor::showChoicePrompt(
    const juce::Rectangle<int> anchorBounds,
    juce::StringArray choices,
    const int selectedIndex,
    std::function<void(int)> onSelect)
{
    if (choices.isEmpty())
        return;

    dismissChoicePrompt();
    choicePrompt = std::make_unique<ChoicePopup>(
        anchorBounds,
        std::move(choices),
        selectedIndex,
        std::move(onSelect),
        [this] { dismissChoicePrompt(); });
    addAndMakeVisible(*choicePrompt);
    choicePrompt->setBounds(getLocalBounds());
    choicePrompt->toFront(true);
}

void PluginEditor::showOfflineSourcePrompt()
{
    refreshOfflineSelectionButtons();
    juce::StringArray names { "ALL" };
    std::vector<juce::String> choiceIds { juce::String() };
    const auto selectedId = audioProcessor.getSelectedOfflineSourceId();
    auto selectedIndex = 0;

    for (const auto& choice : offlineSourceTakeChoices)
    {
        if (std::find(choiceIds.begin(), choiceIds.end(), choice.sourceId) != choiceIds.end())
            continue;

        choiceIds.push_back(choice.sourceId);
        names.add(choice.sourceName);

        if (choice.sourceId == selectedId)
            selectedIndex = names.size() - 1;
    }

    showChoicePrompt(getLocalArea(&sourceButton, sourceButton.getLocalBounds()),
                     std::move(names), selectedIndex,
                     [this, selectedIds = std::move(choiceIds)] (const int index)
                     {
                         if (! juce::isPositiveAndBelow(index, static_cast<int>(selectedIds.size())))
                             return;

                         audioProcessor.setSelectedOfflineSourceId(
                             selectedIds[static_cast<size_t>(index)]);
                         refreshOfflineSelectionButtons();
                         scopeDisplay.refreshWaveform();
                     });
}

void PluginEditor::showOfflineTakePrompt()
{
    refreshOfflineSelectionButtons();
    juce::StringArray names;
    std::vector<juce::String> choiceIds;
    const auto selectedSourceId = audioProcessor.getSelectedOfflineSourceId();
    const auto selectedTakeId = audioProcessor.getSelectedOfflineTakeId();
    auto selectedIndex = -1;

    for (const auto& choice : offlineSourceTakeChoices)
    {
        if (selectedSourceId.isNotEmpty() && choice.sourceId != selectedSourceId)
            continue;
        const auto takeSelection = juce::String(choice.takeNumber);
        if (std::find(choiceIds.begin(), choiceIds.end(), takeSelection) != choiceIds.end())
            continue;

        choiceIds.push_back(takeSelection);
        names.add(takeSelection);

        if (takeSelection == selectedTakeId)
            selectedIndex = names.size() - 1;
    }

    showChoicePrompt(getLocalArea(&takeButton, takeButton.getLocalBounds()),
                     std::move(names), selectedIndex,
                     [this, selectedIds = std::move(choiceIds)] (const int index)
                     {
                         if (! juce::isPositiveAndBelow(index, static_cast<int>(selectedIds.size())))
                             return;

                         audioProcessor.setSelectedOfflineTakeId(
                             selectedIds[static_cast<size_t>(index)]);
                         refreshOfflineSelectionButtons();
                         scopeDisplay.refreshWaveform();
                     });
}

void PluginEditor::refreshOfflineSelectionButtons()
{
    auto latestChoices = audioProcessor.getOfflineSourceTakeChoices();
    const auto choicesChanged = latestChoices != offlineSourceTakeChoices;
    offlineSourceTakeChoices = std::move(latestChoices);
    if (choicesChanged && audioProcessor.isOfflineMode() && ! offlineSourceTakeChoices.empty())
    {
        offlineUpdateStatusMinimumEndMilliseconds =
            juce::Time::getMillisecondCounterHiRes() + 250.0;
        offlineUpdateLabel.setText("UPDATING 00", juce::dontSendNotification);
    }
    auto sourceId = audioProcessor.getSelectedOfflineSourceId();
    auto takeId = audioProcessor.getSelectedOfflineTakeId();
    auto selectedTakeNumber = 0;
    std::vector<int> availableTakeNumbers;
    if (! offlineSourceTakeChoices.empty())
    {
        const auto sourceIsValid = sourceId.isEmpty()
            || std::any_of(offlineSourceTakeChoices.begin(), offlineSourceTakeChoices.end(),
                           [&sourceId] (const auto& choice)
                           {
                               return choice.sourceId == sourceId;
                           });

        if (! sourceIsValid)
        {
            audioProcessor.setSelectedOfflineSourceId({});
            sourceId.clear();
        }

        for (const auto& choice : offlineSourceTakeChoices)
        {
            if (sourceId.isNotEmpty() && choice.sourceId != sourceId)
                continue;

            if (std::find(availableTakeNumbers.begin(), availableTakeNumbers.end(),
                          choice.takeNumber) == availableTakeNumbers.end())
                availableTakeNumbers.push_back(choice.takeNumber);
        }

        std::sort(availableTakeNumbers.begin(), availableTakeNumbers.end());
        const auto requestedTakeNumber = takeId.getIntValue();
        const auto takeIsValid = std::find(availableTakeNumbers.begin(),
                                           availableTakeNumbers.end(),
                                           requestedTakeNumber) != availableTakeNumbers.end();
        if (takeIsValid)
        {
            selectedTakeNumber = requestedTakeNumber;
            takeId = juce::String(selectedTakeNumber);
            if (audioProcessor.getSelectedOfflineTakeId() != takeId)
                audioProcessor.setSelectedOfflineTakeId(takeId);
        }
        else if (! availableTakeNumbers.empty())
        {
            selectedTakeNumber = availableTakeNumbers.front();
            takeId = juce::String(selectedTakeNumber);
            audioProcessor.setSelectedOfflineTakeId(takeId);
        }
    }

    const auto araAvailable = audioProcessor.isARAAvailable();
    sourceButton.setEnabled(araAvailable && ! offlineSourceTakeChoices.empty());
    takeButton.setEnabled(araAvailable && ! availableTakeNumbers.empty());
    sourceButton.setToggleState(sourceId.isNotEmpty(), juce::dontSendNotification);
    takeButton.setToggleState(false, juce::dontSendNotification);
    sourceButton.setTooltip(sourceId.isEmpty() ? "SOURCE: ALL" : "SOURCE: SELECTED");
    takeButton.setTooltip(selectedTakeNumber > 0
        ? "TAKE: " + juce::String(selectedTakeNumber)
        : "TAKE: NONE");
}

void PluginEditor::dismissChoicePrompt()
{
    choicePrompt.reset();
}

void PluginEditor::showAboutPopup()
{
    dismissChoicePrompt();
    dismissAboutPopup();
    aboutPopup = std::make_unique<AboutPopup>(
        [safeEditor = juce::Component::SafePointer<PluginEditor>(this)]
        {
            if (safeEditor != nullptr)
                safeEditor->dismissAboutPopup();
        });
    addAndMakeVisible(*aboutPopup);
    aboutPopup->setBounds(getLocalBounds());
    aboutPopup->toFront(true);
    aboutPopup->grabKeyboardFocus();
}

void PluginEditor::dismissAboutPopup()
{
    aboutPopup.reset();
    repaint();
}

void PluginEditor::timerCallback()
{
    const auto offline = audioProcessor.isOfflineMode();
    realtimeButton.setToggleState(! offline, juce::dontSendNotification);
    offlineButton.setToggleState(offline, juce::dontSendNotification);
    fullSourceButton.setToggleState(audioProcessor.isScopeFullSourceView(),
                                    juce::dontSendNotification);
    refreshOfflineSelectionButtons();
    if (offline && offlineSourceTakeChoices.empty())
        offlineUpdateLabel.setText("NO FILES", juce::dontSendNotification);
    else if (offline && offlineUpdateLabel.getText() == "NO FILES")
        offlineUpdateLabel.setText("UPDATING 00", juce::dontSendNotification);
    else if (! offline && offlineUpdateLabel.getText() == "NO FILES")
        offlineUpdateLabel.setText({}, juce::dontSendNotification);
    if (offline && ! offlineSourceTakeChoices.empty())
    {
        const auto progress = juce::jlimit(0, 100, audioProcessor.getOfflineAnalysisProgress());
        const auto minimumStatusTimeActive = juce::Time::getMillisecondCounterHiRes()
            < offlineUpdateStatusMinimumEndMilliseconds;
        if (progress < 100 || minimumStatusTimeActive)
        {
            const auto displayedProgress = progress >= 100 ? 99 : progress;
            offlineUpdateLabel.setText(
                "UPDATING " + juce::String(displayedProgress).paddedLeft('0', 2),
                juce::dontSendNotification);
        }
        else if (offlineUpdateLabel.getText().startsWith("UPDATING"))
        {
            offlineUpdateLabel.setText("UPDATED", juce::dontSendNotification);
        }
    }
    sourceButton.setEnabled(offline && sourceButton.isEnabled());
    takeButton.setEnabled(offline && takeButton.isEnabled());
    refreshButton.setEnabled(offline);
    clearButton.setEnabled(! offline);
    freezeButton.setEnabled(! offline);
    fullSourceButton.setEnabled(activePage == AnalyzerPage::scope);
    settingsButton.setEnabled(true);
    freezeButton.setToggleState(activePage == AnalyzerPage::frequency
                                    ? audioProcessor.getFrequencySpectrum().isFrozen()
                                    : activePage == AnalyzerPage::correlation
                                        ? audioProcessor.getCorrelationSpectrum().isFrozen()
                                    : activePage == AnalyzerPage::level
                                        ? audioProcessor.getLevelMeter().isFrozen()
                                    : scopeFrozen,
                                juce::dontSendNotification);

    if (editorSizeSavePending
        && juce::Time::getMillisecondCounterHiRes() >= editorSizeSaveDeadlineMilliseconds)
    {
        audioProcessor.setLastEditorSize(pendingEditorSize.x, pendingEditorSize.y);
        editorSizeSavePending = false;
    }
}

void PluginEditor::paint(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colours::black);
}

void PluginEditor::resized()
{
    if (getWidth() > 0 && getHeight() > 0)
    {
        pendingEditorSize = { getWidth(), getHeight() };
        editorSizeSaveDeadlineMilliseconds = juce::Time::getMillisecondCounterHiRes() + 250.0;
        editorSizeSavePending = true;
    }

    auto area = getLocalBounds().reduced(ana::ui::gap.pixels());
    auto controlsRow = area.removeFromTop(ana::ui::controlHeight);
    const std::array<ControlButton*, 4> modeButtons {
        &frequencyButton, &correlationButton, &levelButton, &scopeButton
    };
    for (auto* button : modeButtons)
    {
        button->setVisible(true);
        button->setBounds(controlsRow.removeFromLeft(
            std::min(button->getPreferredWidth(), controlsRow.getWidth())));
        if (button != modeButtons.back())
            ana::ui::gap.removeFromLeft(controlsRow);
    }
    ana::ui::gap.removeFromLeft(controlsRow);
    controlsButton.setBounds(controlsRow.removeFromLeft(
        std::min(controlsButton.getPreferredWidth(), controlsRow.getWidth())));

    mixolveButton.setVisible(true);
    mixolveButton.setBounds(controlsRow.removeFromRight(
        std::min(mixolveButton.getPreferredWidth(), controlsRow.getWidth())));
    ana::ui::gap.removeFromRight(controlsRow);
    settingsButton.setVisible(true);
    settingsButton.setBounds(controlsRow.removeFromRight(
        std::min(settingsButton.getPreferredWidth(), controlsRow.getWidth())));
    ana::ui::gap.removeFromRight(controlsRow);
    ana::ui::gap.removeFromLeft(controlsRow);

    const std::array<juce::Component*, 9> collapsibleControls {
        &realtimeButton, &clearButton, &freezeButton, &fullSourceButton,
        &offlineButton, &sourceButton, &takeButton, &refreshButton, &offlineUpdateLabel
    };
    const std::array<int, 9> controlWidths {
        realtimeButton.getPreferredWidth(), clearButton.getPreferredWidth(),
        freezeButton.getPreferredWidth(), fullSourceButton.getPreferredWidth(),
        offlineButton.getPreferredWidth(), sourceButton.getPreferredWidth(),
        takeButton.getPreferredWidth(), refreshButton.getPreferredWidth(),
        ana::ui::textControlWidth(11)
    };
    auto keepPlacing = mainControlsVisible;
    auto placedAny = false;
    for (size_t index = 0; index < collapsibleControls.size(); ++index)
    {
        auto* component = collapsibleControls[index];
        const auto requiredWidth = controlWidths[index]
            + (placedAny ? ana::ui::gap.pixels() : 0);
        if (! keepPlacing || controlsRow.getWidth() < requiredWidth)
        {
            keepPlacing = false;
            component->setVisible(false);
            continue;
        }

        if (placedAny)
            ana::ui::gap.removeFromLeft(controlsRow);
        component->setVisible(true);
        component->setBounds(controlsRow.removeFromLeft(controlWidths[index]));
        placedAny = true;
    }

    ana::ui::gap.removeFromTop(area);
    scopeDisplay.setBounds(area);
    frequencyDisplay.setBounds(area);
    correlationDisplay.setBounds(area);
    levelDisplay.setBounds(area);

    const auto popupWidth = std::min(area.getWidth(),
        std::min(420, std::max(320, juce::roundToInt(static_cast<float>(area.getWidth()) * 0.42f))));
    settingsComponent.setBounds(area.removeFromRight(popupWidth));

    if (showingAnalyzerSettings)
        settingsComponent.toFront(false);

    if (rightEdgeResizer != nullptr)
    {
        rightEdgeResizer->setBounds(getWidth() - ana::ui::gap.pixels(), 0,
                                    ana::ui::gap.pixels(),
                                    std::max(0, getHeight() - ana::ui::gap.pixels()));
        rightEdgeResizer->toFront(false);
    }
    if (bottomEdgeResizer != nullptr)
    {
        bottomEdgeResizer->setBounds(0, getHeight() - ana::ui::gap.pixels(),
                                     std::max(0, getWidth() - ana::ui::gap.pixels()),
                                     ana::ui::gap.pixels());
        bottomEdgeResizer->toFront(false);
    }

    if (choicePrompt != nullptr)
    {
        choicePrompt->setBounds(getLocalBounds());
        choicePrompt->toFront(true);
    }
    if (aboutPopup != nullptr)
    {
        aboutPopup->setBounds(getLocalBounds());
        aboutPopup->toFront(true);
    }
}
