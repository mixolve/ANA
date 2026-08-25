#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "UiStyle.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
constexpr float minimumCrossoverGapHz = 1.0f;
constexpr int bandButtonWidth = 60;
constexpr int bandZoomValueWidth = 64;
constexpr int bandRangeSliderHeight = 14;
constexpr int bandZoomSliderWidth = bandRangeSliderHeight;
constexpr int minimumBandHeight = ana::ui::gap * 3 + ana::ui::controlHeight + bandRangeSliderHeight;
constexpr int minimumEditorWidth = 1232;
constexpr int minimumEditorHeight = ana::ui::gap * 3 + ana::ui::controlHeight
    + static_cast<int>(ana::MultibandScope::numBands) * minimumBandHeight;
constexpr int maximumEditorSize = 32768;
constexpr int defaultEditorWidth = 1024;
constexpr int defaultEditorHeight = 720;
constexpr int waveformRightInset = ana::ui::gap + bandZoomSliderWidth;
constexpr int offlineUpdateWidth = 300;
constexpr std::array<const char*, 6> scopeModeButtonNames { "LR", "LEFT", "RIGHT", "MS", "MID", "SIDE" };
constexpr std::array<ana::ScopeChannelMode, 6> scopeModeButtonModes {
    ana::ScopeChannelMode::lr,
    ana::ScopeChannelMode::left,
    ana::ScopeChannelMode::right,
    ana::ScopeChannelMode::ms,
    ana::ScopeChannelMode::mid,
    ana::ScopeChannelMode::side
};

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

double parseFrequency(const juce::String& text)
{
    const auto trimmed = text.trim().toLowerCase();
    const auto multiplier = trimmed.containsChar('k') ? 1000.0 : 1.0;
    return trimmed.getDoubleValue() * multiplier;
}

juce::String formatZoomValue(const float decibels)
{
    const auto displayValue = std::abs(decibels) < 0.05f ? 0.0f : decibels;
    return (displayValue > 0.0f ? "+" : "") + juce::String(displayValue, 1);
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
        const auto normalizedX = juce::jlimit(
            0.0f, 1.0f,
            (static_cast<float>(sourceIndex) - firstSourcePosition) / visibleSourceLength);
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

AnaScopeButton::AnaScopeButton(juce::String text)
    : juce::Button(std::move(text))
{
    setWantsKeyboardFocus(false);
}

void AnaScopeButton::paintButton(juce::Graphics& graphics,
                                 const bool,
                                 const bool shouldDrawButtonAsDown)
{
    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    auto fill = shouldDrawButtonAsDown ? ana::ui::grey700 : ana::ui::field;

    graphics.setColour(fill);
    graphics.fillRect(bounds);
    graphics.setColour(getToggleState() ? ana::ui::accent : ana::ui::grey500);
    graphics.drawRect(bounds, getToggleState() ? 1.5f : 1.0f);
    graphics.setColour(isEnabled() ? ana::ui::white : ana::ui::grey500);
    graphics.setFont(ana::ui::makeFont());
    graphics.drawFittedText(getButtonText(), getLocalBounds().reduced(ana::ui::gap, 1),
                            juce::Justification::centred, 1);
}

juce::Slider::SliderLayout AnaSliderLookAndFeel::getSliderLayout(juce::Slider& slider)
{
    if (slider.getSliderStyle() == juce::Slider::LinearBarVertical)
        return { slider.getLocalBounds(), {} };

    return juce::LookAndFeel_V4::getSliderLayout(slider);
}

void AnaSliderLookAndFeel::drawLinearSlider(juce::Graphics& graphics,
                                            const int x, const int y, const int width, const int height,
                                            const float sliderPosition,
                                            const float minimumSliderPosition,
                                            const float maximumSliderPosition,
                                            const juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style == juce::Slider::LinearBarVertical)
    {
        const auto bounds = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
                                                    static_cast<float>(width), static_cast<float>(height)).reduced(0.5f);
        const auto markerY = juce::jlimit(bounds.getY(), bounds.getBottom(), sliderPosition);
        const auto neutralProportion = static_cast<float>(slider.valueToProportionOfLength(0.0));
        const auto neutralY = juce::jmap(neutralProportion, bounds.getBottom(), bounds.getY());
        const auto fillTop = std::min(markerY, neutralY);
        const auto fillBottom = std::max(markerY, neutralY);
        graphics.setColour(ana::ui::field);
        graphics.fillRect(bounds);
        graphics.setColour(ana::ui::accent.withAlpha(slider.isEnabled() ? 0.22f : 0.06f));
        graphics.fillRect(bounds.withTop(fillTop).withBottom(fillBottom));
        graphics.setColour(ana::ui::grey500);
        graphics.fillRect(bounds.getX(), neutralY - 0.5f, bounds.getWidth(), 1.0f);
        graphics.setColour(slider.isEnabled() ? ana::ui::accent : ana::ui::grey500);
        graphics.fillRect(bounds.getX(), markerY - 1.0f, bounds.getWidth(), 2.0f);
        graphics.setColour(ana::ui::grey500);
        graphics.drawRect(bounds, 1.0f);
        return;
    }

    if (style != juce::Slider::LinearHorizontal && style != juce::Slider::LinearBar)
    {
        juce::LookAndFeel_V4::drawLinearSlider(graphics, x, y, width, height,
                                               sliderPosition, minimumSliderPosition, maximumSliderPosition,
                                               style, slider);
        return;
    }

    const auto bounds = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
                                                static_cast<float>(width), static_cast<float>(height)).reduced(0.5f);
    const auto markerX = juce::jlimit(bounds.getX(), bounds.getRight(), sliderPosition);

    graphics.setColour(ana::ui::field);
    graphics.fillRect(bounds);
    graphics.setColour(ana::ui::accent.withAlpha(slider.isEnabled() ? 0.18f : 0.06f));
    graphics.fillRect(bounds.withRight(markerX));
    graphics.setColour(slider.isEnabled() ? ana::ui::accent : ana::ui::grey500);
    graphics.fillRect(markerX - 1.0f, bounds.getY(), 2.0f, bounds.getHeight());
    graphics.setColour(ana::ui::grey500);
    graphics.drawRect(bounds, 1.0f);

    if (style == juce::Slider::LinearBar)
    {
        graphics.setColour(slider.isEnabled() ? ana::ui::white : ana::ui::grey500);
        graphics.setFont(ana::ui::makeFont());
        graphics.drawFittedText(slider.getTextFromValue(slider.getValue()),
                                bounds.toNearestInt().reduced(ana::ui::gap, 1),
                                juce::Justification::centred, 1);
    }
}

void AnaSliderLookAndFeel::drawScrollbar(juce::Graphics& graphics,
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
    graphics.setColour(ana::ui::accent.withAlpha(isMouseDown ? 1.0f : 0.65f));
    graphics.fillRect(thumb);
}

juce::Label* AnaSliderLookAndFeel::createSliderTextBox(juce::Slider& slider)
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

AnaParameterControl::AnaParameterControl(juce::AudioProcessorValueTreeState& state,
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
    valueEditor.setColour(juce::TextEditor::highlightColourId, ana::ui::accent.withAlpha(0.45f));
    valueEditor.onReturnKey = [this] { hideValueEditor(false); };
    valueEditor.onEscapeKey = [this] { hideValueEditor(true); };
    valueEditor.onFocusLost = [this]
    {
        if (valueEditorActive)
            hideValueEditor(false);
    };
    addChildComponent(valueEditor);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(state, parameterId, slider);
}

AnaParameterControl::~AnaParameterControl()
{
    stopTimer();
    slider.setLookAndFeel(nullptr);
    setLookAndFeel(nullptr);
}

void AnaParameterControl::setInteractionEnabled(const bool shouldEnable)
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

juce::StringArray AnaParameterControl::getChoiceNames() const
{
    return choiceParameter != nullptr ? choiceParameter->choices : juce::StringArray();
}

int AnaParameterControl::getSelectedChoiceIndex() const noexcept
{
    return choiceParameter != nullptr ? juce::roundToInt(slider.getValue()) : -1;
}

void AnaParameterControl::setSelectedChoiceIndex(const int choiceIndex)
{
    if (choiceParameter == nullptr || ! juce::isPositiveAndBelow(choiceIndex, choiceParameter->choices.size()))
        return;

    slider.setValue(choiceIndex, juce::sendNotificationSync);
}

void AnaParameterControl::setSelected(const bool shouldSelect)
{
    const auto nextSelected = shouldSelect && interactionEnabled;

    if (selected == nextSelected)
        return;

    selected = nextSelected;
    repaint();
}

void AnaParameterControl::paint(juce::Graphics& graphics)
{
    graphics.setColour(pointerDown && pressHighlighted && pressRegion == PressRegion::title
                           ? ana::ui::grey700 : ana::ui::field);
    graphics.fillRect(titleBounds);
    graphics.setColour(pointerDown && pressHighlighted && pressRegion == PressRegion::value
                           ? ana::ui::grey700 : ana::ui::field);
    graphics.fillRect(valueBounds);
    graphics.setColour(selected ? ana::ui::accent : ana::ui::grey500);
    graphics.drawRect(titleBounds, selected ? 2 : 1);
    graphics.setColour(ana::ui::grey500);
    graphics.drawRect(valueBounds, 1);
    graphics.setColour(interactionEnabled ? ana::ui::white : ana::ui::grey500);
    graphics.setFont(ana::ui::makeFont());
    graphics.drawFittedText(longPressArmed ? "RESET?" : titleText, titleBounds.reduced(12, 1),
                            juce::Justification::centredLeft, 1);
    graphics.drawFittedText(interactionEnabled ? slider.getTextFromValue(slider.getValue()) : "OFF",
                            valueBounds.reduced(ana::ui::gap, 1),
                            juce::Justification::centred, 1);
}

void AnaParameterControl::resized()
{
    auto row = getLocalBounds();

    if (! compact)
    {
        const auto availableWidth = std::max(0, row.getWidth() - ana::ui::gap);
        titleBounds = row.removeFromLeft(availableWidth / 2);
        row.removeFromLeft(std::min(ana::ui::gap, row.getWidth()));
    }
    else
    {
        titleBounds = {};
    }

    valueBounds = row;
    slider.setBounds(valueBounds);
    valueEditor.setBounds(valueBounds);
}

void AnaParameterControl::mouseDown(const juce::MouseEvent& event)
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

void AnaParameterControl::mouseDrag(const juce::MouseEvent& event)
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

void AnaParameterControl::mouseUp(const juce::MouseEvent& event)
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

void AnaParameterControl::commitPendingEditor()
{
    hideValueEditor(false);
}

void AnaParameterControl::mouseExit(const juce::MouseEvent&)
{
    if (! pointerDown)
        return;

    pressHighlighted = false;
    longPressArmed = false;
    stopTimer();
    repaint();
}

void AnaParameterControl::timerCallback()
{
    stopTimer();

    if (! pointerDown || dragDetected || pressRegion != PressRegion::title || ! pressHighlighted)
        return;

    longPressArmed = true;
    repaint();
}

void AnaParameterControl::showValueEditor()
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

void AnaParameterControl::hideValueEditor(const bool discardChanges)
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

void AnaParameterControl::resetToDefault()
{
    if (parameter == nullptr || ! interactionEnabled)
        return;

    slider.setValue(parameter->convertFrom0to1(parameter->getDefaultValue()),
                    juce::sendNotificationSync);
}

AnaChoicePrompt::AnaChoicePrompt(juce::Rectangle<int> anchorBoundsIn,
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
        auto button = std::make_unique<AnaScopeButton>(choices[index]);
        button->setToggleState(index == selectedIndex, juce::dontSendNotification);
        button->onClick = [this, index] { choose(index); };
        addAndMakeVisible(*button);
        choiceButtons.push_back(std::move(button));
    }
}

void AnaChoicePrompt::paint(juce::Graphics& graphics)
{
    graphics.setColour(juce::Colours::black.withAlpha(0.55f));
    graphics.fillAll();
    graphics.setColour(ana::ui::grey700);
    graphics.fillRect(panelBounds);
    graphics.setColour(ana::ui::grey500);
    graphics.drawRect(panelBounds, 1);
}

void AnaChoicePrompt::resized()
{
    const auto itemCount = static_cast<int>(choiceButtons.size());
    const auto contentHeight = itemCount * ana::ui::controlHeight
        + std::max(0, itemCount - 1) * ana::ui::gap;
    auto contentWidth = 0.0f;
    for (const auto& choice : choices)
    {
        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText(ana::ui::makeFont(), choice, 0.0f, 0.0f);
        contentWidth = std::max(contentWidth,
                                glyphs.getBoundingBox(0, glyphs.getNumGlyphs(), true).getWidth());
    }
    const auto desiredPanelWidth = juce::roundToInt(std::ceil(contentWidth))
        + ana::ui::gap * 4;
    const auto panelWidth = std::max(1, std::min(
        std::max(anchorBounds.getWidth(), desiredPanelWidth),
        getWidth() - ana::ui::gap * 2));
    const auto panelHeight = std::min(getHeight() - ana::ui::gap * 2,
                                      contentHeight + ana::ui::gap * 2);
    panelBounds = juce::Rectangle<int>(panelWidth, std::max(1, panelHeight));
    panelBounds.setPosition(anchorBounds.getPosition());
    panelBounds = panelBounds.constrainedWithin(getLocalBounds().reduced(ana::ui::gap));

    auto area = panelBounds.reduced(ana::ui::gap);
    for (size_t index = 0; index < choiceButtons.size(); ++index)
    {
        choiceButtons[index]->setBounds(area.removeFromTop(ana::ui::controlHeight));

        if (index + 1 < choiceButtons.size())
            area.removeFromTop(ana::ui::gap);
    }
}

void AnaChoicePrompt::mouseDown(const juce::MouseEvent& event)
{
    if (! panelBounds.contains(event.getPosition()))
        close();
}

void AnaChoicePrompt::choose(const int index)
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

void AnaChoicePrompt::close()
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

AnaLocalChoiceControl::AnaLocalChoiceControl(juce::String title)
    : titleText(std::move(title))
{
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);
    choices.add("ALL");
}

void AnaLocalChoiceControl::setChoices(juce::StringArray newChoices,
                                       const int newSelectedIndex)
{
    if (newChoices.isEmpty())
        newChoices.add("ALL");

    choices = std::move(newChoices);
    selectedIndex = juce::jlimit(0, choices.size() - 1, newSelectedIndex);
    repaint();
}

void AnaLocalChoiceControl::setSelectedChoiceIndex(const int newSelectedIndex,
                                                   const bool sendChange)
{
    const auto nextIndex = juce::jlimit(0, std::max(0, choices.size() - 1), newSelectedIndex);

    if (selectedIndex == nextIndex)
        return;

    selectedIndex = nextIndex;
    repaint();

    if (sendChange && onSelectionChanged)
        onSelectionChanged(selectedIndex);
}

void AnaLocalChoiceControl::paint(juce::Graphics& graphics)
{
    graphics.setColour(ana::ui::field);
    graphics.fillRect(titleBounds);
    graphics.setColour(pointerDown ? ana::ui::grey700 : ana::ui::field);
    graphics.fillRect(valueBounds);
    graphics.setColour(ana::ui::grey500);
    graphics.drawRect(titleBounds, 1);
    graphics.drawRect(valueBounds, 1);
    graphics.setColour(isEnabled() ? ana::ui::white : ana::ui::grey500);
    graphics.setFont(ana::ui::makeFont());
    graphics.drawFittedText(titleText, titleBounds.reduced(ana::ui::gap, 1),
                            juce::Justification::centredLeft, 1);
    graphics.drawFittedText(choices[selectedIndex], valueBounds.reduced(ana::ui::gap, 1),
                            juce::Justification::centred, 1);
}

void AnaLocalChoiceControl::resized()
{
    auto area = getLocalBounds();
    titleBounds = area.removeFromLeft(area.getWidth() / 2);
    valueBounds = area;
}

void AnaLocalChoiceControl::mouseDown(const juce::MouseEvent& event)
{
    pointerDown = isEnabled() && valueBounds.contains(event.getPosition());
    repaint();
}

void AnaLocalChoiceControl::mouseUp(const juce::MouseEvent& event)
{
    const auto shouldOpen = pointerDown && valueBounds.contains(event.getPosition());
    pointerDown = false;
    repaint();

    if (shouldOpen && onChoiceRequested)
        onChoiceRequested();
}

void AnaLocalChoiceControl::mouseExit(const juce::MouseEvent&)
{
    if (! isMouseButtonDown())
    {
        pointerDown = false;
        repaint();
    }
}

void AnaRangeSlider::paint(juce::Graphics& graphics)
{
    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    const auto startX = juce::jmap(rangeStart, bounds.getX(), bounds.getRight());
    const auto endX = juce::jmap(rangeEnd, bounds.getX(), bounds.getRight());

    graphics.setColour(ana::ui::field);
    graphics.fillRect(bounds);
    graphics.setColour(ana::ui::accent.withAlpha(0.2f));
    graphics.fillRect(bounds.withLeft(startX).withRight(endX));
    graphics.setColour(ana::ui::grey500);
    graphics.drawRect(bounds, 1.0f);
    graphics.setColour(ana::ui::accent);
    graphics.fillRect(startX - 1.0f, bounds.getY(), 2.0f, bounds.getHeight());
    graphics.fillRect(endX - 1.0f, bounds.getY(), 2.0f, bounds.getHeight());
}

void AnaRangeSlider::mouseDown(const juce::MouseEvent& event)
{
    constexpr float handleHitRadius = 8.0f;
    const auto width = static_cast<float>(std::max(1, getWidth() - 1));
    const auto startX = rangeStart * width;
    const auto endX = rangeEnd * width;
    const auto mouseX = static_cast<float>(event.x);

    dragStartRangeStart = rangeStart;
    dragStartRangeEnd = rangeEnd;

    if (std::abs(mouseX - startX) <= handleHitRadius)
        dragMode = DragMode::start;
    else if (std::abs(mouseX - endX) <= handleHitRadius)
        dragMode = DragMode::end;
    else if (mouseX > startX && mouseX < endX)
        dragMode = DragMode::range;
    else
        dragMode = DragMode::none;
}

void AnaRangeSlider::mouseDrag(const juce::MouseEvent& event)
{
    constexpr float minimumRange = 0.01f;
    const auto delta = static_cast<float>(event.getDistanceFromDragStartX())
        / static_cast<float>(std::max(1, getWidth() - 1));

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

void AnaRangeSlider::mouseUp(const juce::MouseEvent&)
{
    dragMode = DragMode::none;
}

void AnaRangeSlider::updateRange(const float newStart, const float newEnd)
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

AnaMultibandScopeComponent::AnaMultibandScopeComponent(AnaAudioProcessor& processorRef)
    : processor(processorRef)
{
    setOpaque(false);
    bandHeightWeights.fill(1.0f);
    singleViewBand = processor.getScopeSingleViewBand();

    for (size_t bandIndex = 0; bandIndex < bandModeButtons.size(); ++bandIndex)
    {
        for (size_t modeIndex = 0; modeIndex < bandModeButtons[bandIndex].size(); ++modeIndex)
        {
            auto button = std::make_unique<AnaScopeButton>(scopeModeButtonNames[modeIndex]);
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

        auto clearButton = std::make_unique<AnaScopeButton>("CLEAR");
        clearButton->onClick = [this, bandIndex] { clearBandHistory(bandIndex); };
        addAndMakeVisible(*clearButton);
        bandClearButtons[bandIndex] = std::move(clearButton);

        auto singleViewButton = std::make_unique<AnaScopeButton>("SVIEW");
        singleViewButton->onClick = [this, bandIndex]
        {
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

        auto zoomValueLabel = std::make_unique<juce::Label>();
        zoomValueLabel->setFont(ana::ui::makeFont());
        zoomValueLabel->setJustificationType(juce::Justification::centred);
        zoomValueLabel->setColour(juce::Label::textColourId, ana::ui::white);
        zoomValueLabel->setColour(juce::Label::backgroundColourId, ana::ui::field);
        zoomValueLabel->setColour(juce::Label::outlineColourId, ana::ui::grey500);
        zoomValueLabel->setBorderSize(juce::BorderSize<int>(1));
        zoomValueLabel->setInterceptsMouseClicks(false, false);
        addAndMakeVisible(*zoomValueLabel);
        bandZoomValueLabels[bandIndex] = std::move(zoomValueLabel);

        auto normalizeButton = std::make_unique<AnaScopeButton>("NORM");
        normalizeButton->onClick = [this, bandIndex] { normalizeBandWithZoom(bandIndex); };
        addAndMakeVisible(*normalizeButton);
        bandNormalizeButtons[bandIndex] = std::move(normalizeButton);

        auto rangeSlider = std::make_unique<AnaRangeSlider>();
        rangeSlider->onRangeChanged = [this] { repaint(); };
        addAndMakeVisible(*rangeSlider);
        bandRangeSliders[bandIndex] = std::move(rangeSlider);

    }

    resetHistory();
    refreshBandModeButtons();
    startTimerHz(60);
}

void AnaMultibandScopeComponent::setFrozen(const bool shouldFreeze)
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

void AnaMultibandScopeComponent::clearHistory()
{
    if (showingOfflineSnapshot)
    {
        displayedOfflineSnapshot.reset();
        clearedBands.fill(true);

        for (size_t bandIndex = 0; bandIndex < bandNormalizeButtons.size(); ++bandIndex)
            processor.setScopeBandNormalized(bandIndex, false);

        refreshBandModeButtons();
        repaint();
        return;
    }

    resetHistory();
}

void AnaMultibandScopeComponent::refreshWaveform()
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
            onOfflineUpdateStatus("UPDATING...");
        processor.requestOfflineAnalysis(offlineAnalysisColumnCount, true);
    }

    repaint();
}

void AnaMultibandScopeComponent::equalizeBandHeights()
{
    bandHeightWeights.fill(1.0f);
    draggedBandSeparator = -1;
    refreshBandModeButtons();
    repaint();
}

void AnaMultibandScopeComponent::refreshDisplaySettings()
{
    if (! showingOfflineSnapshot && ! historyContainsRecordedData)
        resizeHistory(getWaveformColumnCount());

    refreshBandModeButtons();
    repaint();
}

void AnaMultibandScopeComponent::timerCallback()
{
    const auto timeMilliseconds = processor.getScopeTimeMilliseconds();
    const auto waveformColumnCount = getWaveformColumnCount();
    const auto activeBandCount = processor.getActiveSplitCount() + 1;
    std::array<ana::ScopeChannelMode, ana::MultibandScope::numBands> currentModes;
    auto channelModeChanged = false;

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

            for (size_t bandIndex = 0; bandIndex < bandNormalizeButtons.size(); ++bandIndex)
                processor.setScopeBandNormalized(bandIndex, false);

            if (onOfflineUpdateStatus)
                onOfflineUpdateStatus("UPDATING...");
            processor.requestOfflineAnalysis(offlineAnalysisColumnCount, true);
            repaint();
        }

        if (offlineAnalysisColumnCount == 0)
            offlineAnalysisColumnCount = waveformColumnCount;

        processor.requestOfflineAnalysis(offlineAnalysisColumnCount);

        if (const auto snapshot = processor.getOfflineScopeSnapshot())
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

void AnaMultibandScopeComponent::paint(juce::Graphics& graphics)
{
    const auto activeBandCount = processor.getActiveSplitCount() + 1;
    const auto filledStyle = processor.isScopeFilledStyle();
    graphics.setColour(ana::ui::white.withAlpha(processor.getScopeOpacity()));

    for (size_t bandIndex = 0; bandIndex < activeBandCount; ++bandIndex)
    {
        if (clearedBands[bandIndex])
            continue;

        const auto laneBounds = getBandBounds(bandIndex, activeBandCount);

        if (laneBounds.isEmpty())
            continue;

        auto waveformBounds = laneBounds;
        const auto zoomControlsVisible = processor.areScopeZoomControlsVisible();
        if (zoomControlsVisible)
        {
            waveformBounds.setBottom(std::max(waveformBounds.getY() + 1.0f,
                                              waveformBounds.getBottom()
                                                  - static_cast<float>(bandRangeSliderHeight + ana::ui::gap)));
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
                ? &displayedOfflineSnapshot->bands[bandIndex][modeIndex]
                : ! showingOfflineSnapshot ? &historyEnvelopes[bandIndex][modeIndex] : nullptr;

            if (envelope != nullptr)
                drawWaveformEnvelope(graphics, envelope->minimums, envelope->maximums, displayBounds,
                                     rangeStart, rangeEnd,
                                     processor.getScopeVerticalZoomDecibels(bandIndex),
                                     filledStyle);
        }
    }

    graphics.setColour(ana::ui::white.withAlpha(0.42f));

    for (size_t bandIndex = 0; bandIndex < activeBandCount; ++bandIndex)
    {
        auto waveformBounds = getBandBounds(bandIndex, activeBandCount);

        if (waveformBounds.isEmpty())
            continue;

        const auto zoomControlsVisible = processor.areScopeZoomControlsVisible();

        if (zoomControlsVisible)
            waveformBounds.setBottom(std::max(waveformBounds.getY() + 1.0f,
                                              waveformBounds.getBottom()
                                                  - static_cast<float>(bandRangeSliderHeight + ana::ui::gap)));

        const auto lineWidth = zoomControlsVisible
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
            graphics.setColour(ana::ui::white.withAlpha(0.42f));
        }
    }

    graphics.setColour(ana::ui::accent);

    for (size_t bandIndex = 1;
         singleViewBand < 0 && bandIndex < activeBandCount;
         ++bandIndex)
    {
        const auto y = juce::roundToInt(getBandBounds(bandIndex, activeBandCount).getY());
        graphics.fillRect(0, y, getWidth(), 1);
    }
}

void AnaMultibandScopeComponent::resized()
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

void AnaMultibandScopeComponent::resetHistory()
{
    showingOfflineSnapshot = false;
    offlineAnalysisColumnCount = 0;
    offlineSnapshotRevision = 0;
    displayedOfflineSnapshot.reset();
    historyTimeMilliseconds = processor.getScopeTimeMilliseconds();
    historyBandCount = processor.getActiveSplitCount() + 1;
    historyContainsRecordedData = false;
    clearedBands.fill(false);

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

    repaint();
}

void AnaMultibandScopeComponent::resizeHistory(const size_t newColumnCount)
{
    const auto targetColumnCount = std::max<size_t>(1, newColumnCount);

    for (auto& band : historyEnvelopes)
    {
        for (auto& envelope : band)
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
    }

    repaint();
}

void AnaMultibandScopeComponent::clearBandHistory(const size_t bandIndex)
{
    if (bandIndex >= historyBandCount)
        return;

    const auto bandBounds = getBandBounds(bandIndex, historyBandCount).toNearestInt();
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

void AnaMultibandScopeComponent::resetColumnAccumulator()
{
    for (auto& band : columnMinimums)
        band.fill(std::numeric_limits<float>::max());
    for (auto& band : columnMaximums)
        band.fill(std::numeric_limits<float>::lowest());
}

void AnaMultibandScopeComponent::normalizeBandWithZoom(const size_t bandIndex)
{
    if (! processor.isOfflineMode()
        || ! showingOfflineSnapshot
        || displayedOfflineSnapshot == nullptr
        || bandIndex >= historyBandCount
        || clearedBands[bandIndex])
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
        const auto& envelope = displayedOfflineSnapshot->bands[bandIndex]
            [displayedModes.indices[displayIndex]];

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

void AnaMultibandScopeComponent::refreshBandModeButtons()
{
    const auto activeBandCount = processor.getActiveSplitCount() + 1;
    const auto persistedSingleViewBand = processor.getScopeSingleViewBand();

    if (persistedSingleViewBand != singleViewBand)
        singleViewBand = persistedSingleViewBand;

    if (singleViewBand >= static_cast<int>(activeBandCount))
    {
        singleViewBand = -1;
        processor.setScopeSingleViewBand(-1);
    }

    constexpr int buttonWidth = bandButtonWidth;
    constexpr int buttonHeight = ana::ui::controlHeight;
    constexpr int buttonGap = ana::ui::gap;
    constexpr int zoomSliderWidth = bandZoomSliderWidth;
    constexpr int zoomValueWidth = bandZoomValueWidth;
    const auto offlineMode = processor.isOfflineMode();
    const auto showZoomControls = processor.areScopeZoomControlsVisible();
    const auto showMonitorControls = processor.areScopeMonitorControlsVisible();
    const auto showOtherControls = processor.areScopeOtherControlsVisible();
    const auto clearButtonX = showMonitorControls
        ? static_cast<int>(bandModeButtons.front().size()) * (buttonWidth + buttonGap)
        : 0;
    const auto singleViewButtonX = clearButtonX + buttonWidth + buttonGap;
    const auto normalizationAvailable = offlineMode
        && showingOfflineSnapshot
        && displayedOfflineSnapshot != nullptr;
    const juce::ScopedValueSetter<bool> controlUpdate(updatingBandControls, true);

    for (size_t bandIndex = 0; bandIndex < bandModeButtons.size(); ++bandIndex)
    {
        const auto isActive = bandIndex < activeBandCount;
        const auto isVisibleBand = isActive
            && (singleViewBand < 0 || singleViewBand == static_cast<int>(bandIndex));
        const auto selectedMode = processor.getScopeChannelMode(bandIndex);
        const auto verticalZoomDecibels = processor.getScopeVerticalZoomDecibels(bandIndex);
        const auto normalized = offlineMode && processor.isScopeBandNormalized(bandIndex);
        const auto laneBounds = isVisibleBand
            ? getBandBounds(bandIndex, activeBandCount).toNearestInt()
            : juce::Rectangle<int>();

        for (size_t modeIndex = 0; modeIndex < bandModeButtons[bandIndex].size(); ++modeIndex)
        {
            auto& button = *bandModeButtons[bandIndex][modeIndex];
            button.setVisible(isVisibleBand && showMonitorControls);
            button.setToggleState(selectedMode == scopeModeButtonModes[modeIndex],
                                  juce::dontSendNotification);

            if (isVisibleBand && showMonitorControls)
            {
                const auto x = static_cast<int>(modeIndex) * (buttonWidth + buttonGap);
                const auto y = laneBounds.getY() + ana::ui::gap;
                button.setBounds(x, y, buttonWidth, buttonHeight);
            }
        }

        auto& clearButton = *bandClearButtons[bandIndex];
        clearButton.setVisible(isVisibleBand && showOtherControls);

        if (isVisibleBand && showOtherControls)
        {
            const auto y = laneBounds.getY() + ana::ui::gap;
            clearButton.setBounds(clearButtonX, y, buttonWidth, buttonHeight);
        }

        auto& singleViewButton = *bandSingleViewButtons[bandIndex];
        singleViewButton.setVisible(isVisibleBand && showOtherControls);
        singleViewButton.setToggleState(singleViewBand == static_cast<int>(bandIndex),
                                        juce::dontSendNotification);

        if (isVisibleBand && showOtherControls)
        {
            const auto y = laneBounds.getY() + ana::ui::gap;
            singleViewButton.setBounds(singleViewButtonX, y, buttonWidth, buttonHeight);
        }

        auto& zoomSlider = *bandZoomSliders[bandIndex];
        auto& zoomValueLabel = *bandZoomValueLabels[bandIndex];
        auto& normalizeButton = *bandNormalizeButtons[bandIndex];
        zoomSlider.setVisible(isVisibleBand && showZoomControls);
        zoomValueLabel.setVisible(isVisibleBand && showZoomControls);
        normalizeButton.setVisible(isVisibleBand && showZoomControls);
        normalizeButton.setEnabled(normalizationAvailable && ! clearedBands[bandIndex]);
        zoomSlider.setValue(verticalZoomDecibels, juce::dontSendNotification);
        zoomSlider.setTooltip("ZOOM " + formatZoomValue(verticalZoomDecibels));
        zoomValueLabel.setText(formatZoomValue(verticalZoomDecibels), juce::dontSendNotification);
        normalizeButton.setToggleState(normalized, juce::dontSendNotification);

        auto& rangeSlider = *bandRangeSliders[bandIndex];
        rangeSlider.setVisible(isVisibleBand && showZoomControls);

        if (isVisibleBand && showZoomControls)
        {
            const auto buttonY = laneBounds.getY() + ana::ui::gap;
            const auto sliderX = std::max(0, getWidth() - zoomSliderWidth);
            const auto rangeBounds = juce::Rectangle<int>(
                0,
                laneBounds.getBottom() - ana::ui::gap - bandRangeSliderHeight,
                std::max(1, sliderX - ana::ui::gap),
                bandRangeSliderHeight);
            const auto zoomValueX = sliderX - buttonGap - zoomValueWidth;
            const auto normalizeButtonX = zoomValueX - buttonGap - buttonWidth;

            rangeSlider.setBounds(rangeBounds);
            zoomSlider.setBounds(sliderX, buttonY, zoomSliderWidth,
                                 std::max(1, rangeBounds.getBottom() - buttonY));
            zoomValueLabel.setBounds(zoomValueX, buttonY, zoomValueWidth, buttonHeight);
            normalizeButton.setBounds(normalizeButtonX, buttonY, buttonWidth, buttonHeight);
        }
    }
}

juce::Rectangle<float> AnaMultibandScopeComponent::getBandBounds(
    const size_t bandIndex, const size_t activeBandCount) const noexcept
{
    if (activeBandCount == 0 || bandIndex >= activeBandCount)
        return {};

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

size_t AnaMultibandScopeComponent::getWaveformColumnCount() const noexcept
{
    const auto drawableWidth = processor.areScopeZoomControlsVisible()
        ? getWidth() - waveformRightInset
        : getWidth() - 1;
    return static_cast<size_t>(std::max(1, drawableWidth));
}

int AnaMultibandScopeComponent::findBandSeparator(
    const int y, const size_t activeBandCount) const noexcept
{
    if (singleViewBand >= 0)
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

void AnaMultibandScopeComponent::mouseMove(const juce::MouseEvent& event)
{
    const auto activeBandCount = processor.getActiveSplitCount() + 1;
    setMouseCursor(findBandSeparator(event.y, activeBandCount) >= 0
        ? juce::MouseCursor::UpDownResizeCursor
        : juce::MouseCursor::NormalCursor);
}

void AnaMultibandScopeComponent::mouseExit(const juce::MouseEvent&)
{
    if (draggedBandSeparator < 0)
        setMouseCursor(juce::MouseCursor::NormalCursor);
}

void AnaMultibandScopeComponent::mouseDown(const juce::MouseEvent& event)
{
    draggedBandSeparator = findBandSeparator(event.y, processor.getActiveSplitCount() + 1);

    if (draggedBandSeparator >= 0)
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
}

void AnaMultibandScopeComponent::mouseDrag(const juce::MouseEvent& event)
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

void AnaMultibandScopeComponent::mouseUp(const juce::MouseEvent& event)
{
    draggedBandSeparator = -1;
    mouseMove(event);
}

std::array<float, ana::OfflineScopeSnapshot::numChannelModes>
AnaMultibandScopeComponent::getRealtimeModeSamples(const size_t bandIndex,
                                                   const size_t sampleIndex) const noexcept
{
    const auto left = incomingSamples[bandIndex][0][sampleIndex];
    const auto right = incomingSamples[bandIndex][1][sampleIndex];
    return { left, right, 0.5f * (left + right), 0.5f * (left - right) };
}

void AnaMultibandScopeComponent::appendHistoryColumn(const size_t activeBandCount)
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

    historyContainsRecordedData = true;
}

void AnaMultibandScopeComponent::renderOfflineSnapshot(
    std::shared_ptr<const ana::OfflineScopeSnapshot> snapshot)
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

    if (receivedNewRevision)
    {
        for (size_t bandIndex = 0; bandIndex < bandNormalizeButtons.size(); ++bandIndex)
            processor.setScopeBandNormalized(bandIndex, false);

        refreshBandModeButtons();

        if (onOfflineUpdateStatus)
            onOfflineUpdateStatus(
                "UPDATED " + juce::Time::getCurrentTime().formatted("%d.%m.%Y %H:%M:%S"));
    }

    repaint();
}

AnaCrossoverSettingsComponent::AnaCrossoverSettingsComponent(AnaAudioProcessor& processorRef)
    : processor(processorRef),
      styleControl(processorRef.getParameters(), AnaAudioProcessor::scopeStyleParameterId,
                   "STYLE",
                   [] (const double value)
                   {
                       return value < 0.5 ? juce::String("FILLED") : juce::String("OUTLINE");
                   }),
      opacityControl(processorRef.getParameters(), AnaAudioProcessor::scopeOpacityParameterId,
                     "OPACITY",
                     [] (const double value)
                     {
                         return juce::String(juce::roundToInt(value));
                     }),
      timeControl(processorRef.getParameters(), AnaAudioProcessor::scopeTimeParameterId,
                  "TIME",
                  [] (const double value)
                  {
                      const auto seconds = value / 1000.0;
                      return juce::String(seconds, seconds < 10.0 ? 1 : 0);
                  })
{
    setOpaque(false);
    settingsViewport.setViewedComponent(&settingsContent, false);
    settingsViewport.setScrollBarsShown(true, false, true, false);
    settingsViewport.setScrollBarThickness(ana::ui::gap);
    settingsViewport.setLookAndFeel(&focusedControlLookAndFeel);
    settingsViewport.setWantsKeyboardFocus(false);
    settingsViewport.setMouseClickGrabsKeyboardFocus(false);
    addAndMakeVisible(settingsViewport);

    const auto configureHeading = [this] (juce::Label& label, const juce::String& text)
    {
        label.setText(text, juce::dontSendNotification);
        label.setFont(ana::ui::makeFont());
        label.setJustificationType(juce::Justification::centredLeft);
        label.setColour(juce::Label::textColourId, ana::ui::white);
        label.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        label.setColour(juce::Label::outlineColourId, ana::ui::grey500);
        label.setBorderSize(juce::BorderSize<int>(1, ana::ui::gap, 1, ana::ui::gap));
        label.setInterceptsMouseClicks(false, false);
        settingsContent.addAndMakeVisible(label);
    };
    configureHeading(headingLabel, "SETTINGS");
    configureHeading(generalHeadingLabel, "GENERAL");
    configureHeading(controlsVisibilityHeadingLabel, "CONTROLS VISIBILITY");
    configureHeading(realtimeHeadingLabel, "REALTIME");
    configureHeading(offlineHeadingLabel, "OFFLINE");

    for (auto* component : std::array<juce::Component*, 10> {
             &addCrossoverButton, &removeCrossoverButton, &equalHeightButton,
             &styleControl, &opacityControl, &zoomControlsButton,
             &monitorControlsButton, &otherControlsButton, &timeControl,
             &alwaysSecondTakeButton })
        settingsContent.addAndMakeVisible(*component);

    addAndMakeVisible(focusedParameterControl);
    addCrossoverButton.onClick = [this] { changeActiveSplitCount(1); };
    removeCrossoverButton.onClick = [this] { changeActiveSplitCount(-1); };
    equalHeightButton.onClick = [this]
    {
        if (onEqualBandHeights)
            onEqualBandHeights();
    };
    const auto displaySettingChanged = [this]
    {
        if (onDisplaySettingsChanged)
            onDisplaySettingsChanged();
    };
    styleControl.onValueChanged = displaySettingChanged;
    opacityControl.onValueChanged = displaySettingChanged;
    for (auto* button : std::array<AnaScopeButton*, 3> {
             &zoomControlsButton, &monitorControlsButton, &otherControlsButton })
    {
        button->setClickingTogglesState(true);
        button->onClick = displaySettingChanged;
    }
    zoomControlsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), AnaAudioProcessor::scopeZoomControlsParameterId, zoomControlsButton);
    monitorControlsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), AnaAudioProcessor::scopeMonitorControlsParameterId, monitorControlsButton);
    otherControlsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), AnaAudioProcessor::scopeOtherControlsParameterId, otherControlsButton);
    alwaysSecondTakeButton.setClickingTogglesState(true);
    alwaysSecondTakeButton.onClick = [this]
    {
        if (onOfflineSelectionChanged)
            onOfflineSelectionChanged(alwaysSecondTakeButton.getToggleState());
    };
    alwaysSecondTakeAttachment =
        std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            processor.getParameters(), AnaAudioProcessor::alwaysSecondTakeParameterId,
            alwaysSecondTakeButton);

    const auto requestChoice = [this] (AnaParameterControl& control)
    {
        if (onChoiceRequested)
            onChoiceRequested(control);
    };
    styleControl.onChoiceRequested = requestChoice;

    const auto focusControl = [this] (AnaParameterControl& control)
    {
        focusParameterControl(control);
    };
    opacityControl.onFocusRequested = focusControl;
    timeControl.onFocusRequested = focusControl;
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

        auto& target = focusedParameterTarget->getSlider();
        const auto value = target.getNormalisableRange().convertFrom0to1(focusedParameterControl.getValue());
        target.setValue(value, juce::sendNotificationSync);
        syncFocusedParameterControl();
    };

    for (size_t index = 0; index < crossoverControls.size(); ++index)
    {
        auto control = std::make_unique<AnaParameterControl>(
            processor.getParameters(),
            AnaAudioProcessor::crossoverParameterIds[index],
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

AnaCrossoverSettingsComponent::~AnaCrossoverSettingsComponent()
{
    settingsContent.removeMouseListener(this);
    settingsViewport.setViewedComponent(nullptr, false);
    settingsViewport.setLookAndFeel(nullptr);
    focusedParameterControl.setLookAndFeel(nullptr);
}

void AnaCrossoverSettingsComponent::paint(juce::Graphics& graphics)
{
    graphics.fillAll(ana::ui::grey800.withAlpha(0.97f));
    graphics.setColour(ana::ui::accent);
    graphics.drawRect(getLocalBounds(), 1);
}

void AnaCrossoverSettingsComponent::resized()
{
    constexpr int headingHeight = ana::ui::controlHeight;
    constexpr int rowHeight = ana::ui::controlHeight;
    constexpr int gap = ana::ui::gap;
    constexpr int rowCount = 19;
    constexpr int regularGapCount = 18;
    constexpr int extraSectionGap = gap;
    const auto contentHeight = rowCount * rowHeight + regularGapCount * gap + extraSectionGap;
    auto innerBounds = getLocalBounds().reduced(gap);
    const auto potentiometerBounds = innerBounds.removeFromBottom(rowHeight);
    innerBounds.removeFromBottom(std::min(gap, innerBounds.getHeight()));
    const auto viewportBounds = innerBounds;
    settingsViewport.setBounds(viewportBounds);
    const auto needsScrollbar = contentHeight > viewportBounds.getHeight();
    const auto scrollbarReserve = needsScrollbar
        ? settingsViewport.getScrollBarThickness() + gap
        : 0;
    const auto contentWidth = std::max(1, viewportBounds.getWidth()
        - scrollbarReserve);
    settingsContent.setSize(contentWidth, std::max(contentHeight, viewportBounds.getHeight()));
    focusedParameterControl.setBounds(viewportBounds.getX(), potentiometerBounds.getY(),
                                      contentWidth, rowHeight);
    auto area = settingsContent.getLocalBounds();

    headingLabel.setBounds(area.removeFromTop(headingHeight));
    area.removeFromTop(gap);
    generalHeadingLabel.setBounds(area.removeFromTop(headingHeight));
    area.removeFromTop(gap);

    auto crossoverButtons = area.removeFromTop(rowHeight);
    const auto crossoverButtonWidth = std::max(0, crossoverButtons.getWidth() - gap) / 2;
    addCrossoverButton.setBounds(crossoverButtons.removeFromLeft(crossoverButtonWidth));
    crossoverButtons.removeFromLeft(std::min(gap, crossoverButtons.getWidth()));
    removeCrossoverButton.setBounds(crossoverButtons);
    area.removeFromTop(gap);

    for (auto& control : crossoverControls)
    {
        control->setBounds(area.removeFromTop(rowHeight));
        area.removeFromTop(gap);
    }

    area.removeFromTop(gap);
    equalHeightButton.setBounds(area.removeFromTop(rowHeight));
    area.removeFromTop(gap);
    styleControl.setBounds(area.removeFromTop(rowHeight));
    area.removeFromTop(gap);
    opacityControl.setBounds(area.removeFromTop(rowHeight));
    area.removeFromTop(gap);

    controlsVisibilityHeadingLabel.setBounds(area.removeFromTop(headingHeight));
    area.removeFromTop(gap);
    zoomControlsButton.setBounds(area.removeFromTop(rowHeight));
    area.removeFromTop(gap);
    monitorControlsButton.setBounds(area.removeFromTop(rowHeight));
    area.removeFromTop(gap);
    otherControlsButton.setBounds(area.removeFromTop(rowHeight));
    area.removeFromTop(gap);

    realtimeHeadingLabel.setBounds(area.removeFromTop(headingHeight));
    area.removeFromTop(gap);
    timeControl.setBounds(area.removeFromTop(rowHeight));
    area.removeFromTop(gap);

    offlineHeadingLabel.setBounds(area.removeFromTop(headingHeight));
    area.removeFromTop(gap);
    alwaysSecondTakeButton.setBounds(area.removeFromTop(rowHeight));
}

void AnaCrossoverSettingsComponent::mouseDown(const juce::MouseEvent& event)
{
    if (event.originalComponent != &settingsContent && event.originalComponent != this)
        return;

    dismissParameterEditors();
    clearFocusedParameterControl();
}

void AnaCrossoverSettingsComponent::timerCallback()
{
    refreshExternalState();
    syncFocusedParameterControl();
}

void AnaCrossoverSettingsComponent::changeActiveSplitCount(const int delta)
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

void AnaCrossoverSettingsComponent::constrainFrequency(const size_t crossoverIndex)
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

void AnaCrossoverSettingsComponent::refreshExternalState()
{
    const auto activeSplitCount = processor.getActiveSplitCount();
    addCrossoverButton.setEnabled(activeSplitCount < crossoverControls.size());
    removeCrossoverButton.setEnabled(activeSplitCount > 0);

    for (size_t index = 0; index < crossoverControls.size(); ++index)
        crossoverControls[index]->setInteractionEnabled(index < activeSplitCount);

    if (focusedParameterTarget != nullptr && ! focusedParameterTarget->isInteractionEnabled())
    {
        focusedParameterTarget->setSelected(false);
        focusedParameterTarget = nullptr;
        focusedParameterControl.setEnabled(false);
    }
}

void AnaCrossoverSettingsComponent::focusParameterControl(AnaParameterControl& control)
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

void AnaCrossoverSettingsComponent::clearFocusedParameterControl()
{
    if (focusedParameterTarget != nullptr)
        focusedParameterTarget->setSelected(false);

    focusedParameterTarget = nullptr;
    focusedParameterControl.setEnabled(false);
}

void AnaCrossoverSettingsComponent::dismissParameterEditors()
{
    styleControl.commitPendingEditor();
    opacityControl.commitPendingEditor();
    timeControl.commitPendingEditor();

    for (auto& control : crossoverControls)
        control->commitPendingEditor();
}

void AnaCrossoverSettingsComponent::syncFocusedParameterControl()
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

AnaAudioProcessorEditor::AnaAudioProcessorEditor(AnaAudioProcessor& processorRef)
    : AudioProcessorEditor(&processorRef)
#if JucePlugin_Enable_ARA
    , AudioProcessorEditorARAExtension(&processorRef)
#endif
    , audioProcessor(processorRef),
      scopeDisplay(processorRef),
      crossoverSettings(processorRef)
{
    for (auto* component : std::array<juce::Component*, 14> {
             &scopeDisplay, &crossoverSettings, &offlineUpdateLabel, &frequencyButton, &phaseButton,
             &scopeButton, &settingsButton, &realtimeButton, &offlineButton, &sourceButton,
             &takeButton, &refreshButton, &clearButton, &freezeButton })
        addAndMakeVisible(*component);

    frequencyButton.setEnabled(false);
    phaseButton.setEnabled(false);
    scopeButton.onClick = [this] { showCrossoverSettings(false); };
    settingsButton.onClick = [this] { showCrossoverSettings(! showingCrossoverSettings); };

    offlineUpdateLabel.setFont(ana::ui::makeFont());
    offlineUpdateLabel.setJustificationType(juce::Justification::centred);
    offlineUpdateLabel.setColour(juce::Label::textColourId, ana::ui::white);
    offlineUpdateLabel.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    offlineUpdateLabel.setColour(juce::Label::outlineColourId, ana::ui::grey500);
    offlineUpdateLabel.setBorderSize(
        juce::BorderSize<int>(1, ana::ui::gap, 1, ana::ui::gap));
    offlineUpdateLabel.setInterceptsMouseClicks(false, false);
    offlineUpdateLabel.setVisible(true);
    scopeDisplay.onOfflineUpdateStatus = [this] (const juce::String& status)
    {
        offlineUpdateLabel.setText(status, juce::dontSendNotification);
    };
    crossoverSettings.onDisplaySettingsChanged = [this] { scopeDisplay.refreshDisplaySettings(); };
    crossoverSettings.onEqualBandHeights = [this] { scopeDisplay.equalizeBandHeights(); };
    crossoverSettings.onOfflineSelectionChanged = [this] (const bool alwaysUseSecondTake)
    {
        audioProcessor.setAlwaysUseSecondOfflineTake(alwaysUseSecondTake);
        refreshOfflineSelectionButtons();
        scopeDisplay.refreshWaveform();
    };
    crossoverSettings.onChoiceRequested = [this] (AnaParameterControl& control)
    {
        showChoicePrompt(control);
    };

    freezeButton.setClickingTogglesState(true);
    freezeButton.onClick = [this] { scopeDisplay.setFrozen(freezeButton.getToggleState()); };
    refreshButton.onClick = [this] { scopeDisplay.refreshWaveform(); };
    clearButton.onClick = [this] { scopeDisplay.clearHistory(); };
    realtimeButton.onClick = [this] { audioProcessor.setOfflineMode(false); };
    realtimeButton.setTooltip("Realtime input mode");
    offlineButton.onClick = [this] { audioProcessor.setOfflineMode(true); };
    offlineButton.setTooltip("ARA offline source mode");
    sourceButton.onClick = [this] { showOfflineSourcePrompt(); };
    takeButton.onClick = [this] { showOfflineTakePrompt(); };

    showCrossoverSettings(false);
    timerCallback();
    startTimerHz(15);
    setResizable(true, false);
    setResizeLimits(minimumEditorWidth, minimumEditorHeight, maximumEditorSize, maximumEditorSize);
    const auto savedSize = audioProcessor.getLastEditorSize();
    setSize(juce::jlimit(minimumEditorWidth, maximumEditorSize,
                         savedSize.x > 0 ? savedSize.x : defaultEditorWidth),
            juce::jlimit(minimumEditorHeight, maximumEditorSize,
                         savedSize.y > 0 ? savedSize.y : defaultEditorHeight));
}

AnaAudioProcessorEditor::~AnaAudioProcessorEditor()
{
    if (getWidth() > 0 && getHeight() > 0)
        audioProcessor.setLastEditorSize(getWidth(), getHeight());
}

void AnaAudioProcessorEditor::showCrossoverSettings(const bool shouldShowSettings)
{
    if (! shouldShowSettings)
        dismissChoicePrompt();

    showingCrossoverSettings = shouldShowSettings;
    scopeButton.setToggleState(true, juce::dontSendNotification);
    settingsButton.setToggleState(shouldShowSettings, juce::dontSendNotification);
    scopeDisplay.setVisible(true);
    crossoverSettings.setVisible(shouldShowSettings);

    if (shouldShowSettings)
        crossoverSettings.toFront(false);

    resized();
    repaint();
}

void AnaAudioProcessorEditor::showChoicePrompt(AnaParameterControl& control)
{
    const auto choices = control.getChoiceNames();

    if (choices.isEmpty())
        return;

    const auto anchorBounds = getLocalArea(&control, control.getValueBounds());
    juce::Component::SafePointer<AnaParameterControl> safeControl(&control);
    showChoicePrompt(anchorBounds, choices, control.getSelectedChoiceIndex(),
        [safeControl] (const int selectedIndex)
        {
            if (safeControl != nullptr)
                safeControl->setSelectedChoiceIndex(selectedIndex);
        });
}

void AnaAudioProcessorEditor::showChoicePrompt(
    const juce::Rectangle<int> anchorBounds,
    juce::StringArray choices,
    const int selectedIndex,
    std::function<void(int)> onSelect)
{
    if (choices.isEmpty())
        return;

    dismissChoicePrompt();
    choicePrompt = std::make_unique<AnaChoicePrompt>(
        anchorBounds,
        std::move(choices),
        selectedIndex,
        std::move(onSelect),
        [this] { dismissChoicePrompt(); });
    addAndMakeVisible(*choicePrompt);
    choicePrompt->setBounds(getLocalBounds());
    choicePrompt->toFront(true);
}

void AnaAudioProcessorEditor::showOfflineSourcePrompt()
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

void AnaAudioProcessorEditor::showOfflineTakePrompt()
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

                         const auto selectedTake = selectedIds[static_cast<size_t>(index)];
                         if (selectedTake != "2"
                             && audioProcessor.shouldAlwaysUseSecondOfflineTake())
                             audioProcessor.setAlwaysUseSecondOfflineTake(false);
                         audioProcessor.setSelectedOfflineTakeId(
                             selectedTake);
                         refreshOfflineSelectionButtons();
                         scopeDisplay.refreshWaveform();
                     });
}

void AnaAudioProcessorEditor::refreshOfflineSelectionButtons()
{
    offlineSourceTakeChoices = audioProcessor.getOfflineSourceTakeChoices();
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
        const auto alwaysUseSecondTake = audioProcessor.shouldAlwaysUseSecondOfflineTake();
        const auto requestedTakeNumber = alwaysUseSecondTake ? 2 : takeId.getIntValue();
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
    takeButton.setToggleState(selectedTakeNumber > 0, juce::dontSendNotification);
    sourceButton.setTooltip(sourceId.isEmpty() ? "SOURCE: ALL" : "SOURCE: SELECTED");
    takeButton.setTooltip(selectedTakeNumber > 0
        ? "TAKE: " + juce::String(selectedTakeNumber)
        : "TAKE: NONE");
}

void AnaAudioProcessorEditor::dismissChoicePrompt()
{
    choicePrompt.reset();
}

void AnaAudioProcessorEditor::timerCallback()
{
    const auto offline = audioProcessor.isOfflineMode();
    realtimeButton.setToggleState(! offline, juce::dontSendNotification);
    offlineButton.setToggleState(offline, juce::dontSendNotification);
    refreshOfflineSelectionButtons();

    if (editorSizeSavePending
        && juce::Time::getMillisecondCounterHiRes() >= editorSizeSaveDeadlineMilliseconds)
    {
        audioProcessor.setLastEditorSize(pendingEditorSize.x, pendingEditorSize.y);
        editorSizeSavePending = false;
    }
}

void AnaAudioProcessorEditor::paint(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colours::black);
}

void AnaAudioProcessorEditor::resized()
{
    if (getWidth() > 0 && getHeight() > 0)
    {
        pendingEditorSize = { getWidth(), getHeight() };
        editorSizeSaveDeadlineMilliseconds = juce::Time::getMillisecondCounterHiRes() + 250.0;
        editorSizeSavePending = true;
    }

    auto area = getLocalBounds().reduced(ana::ui::gap);
    auto controlsRow = area.removeFromTop(ana::ui::controlHeight);
    auto modeArea = controlsRow.removeFromLeft(
        std::min(225, juce::roundToInt(static_cast<float>(controlsRow.getWidth()) * 0.34f)));
    const auto modeButtonWidth = std::max(0, modeArea.getWidth() - ana::ui::gap * 2) / 3;
    frequencyButton.setBounds(modeArea.removeFromLeft(modeButtonWidth));
    modeArea.removeFromLeft(std::min(ana::ui::gap, modeArea.getWidth()));
    phaseButton.setBounds(modeArea.removeFromLeft(modeButtonWidth));
    modeArea.removeFromLeft(std::min(ana::ui::gap, modeArea.getWidth()));
    scopeButton.setBounds(modeArea);

    settingsButton.setBounds(controlsRow.removeFromRight(std::min(95, controlsRow.getWidth())));
    controlsRow.removeFromRight(std::min(ana::ui::gap, controlsRow.getWidth()));

    offlineUpdateLabel.setBounds(controlsRow.removeFromRight(
        std::min(offlineUpdateWidth, controlsRow.getWidth())));
    controlsRow.removeFromRight(std::min(ana::ui::gap, controlsRow.getWidth()));
    refreshButton.setBounds(controlsRow.removeFromRight(std::min(76, controlsRow.getWidth())));
    controlsRow.removeFromRight(std::min(ana::ui::gap, controlsRow.getWidth()));
    takeButton.setBounds(controlsRow.removeFromRight(std::min(68, controlsRow.getWidth())));
    controlsRow.removeFromRight(std::min(ana::ui::gap, controlsRow.getWidth()));
    sourceButton.setBounds(controlsRow.removeFromRight(std::min(76, controlsRow.getWidth())));
    controlsRow.removeFromRight(std::min(ana::ui::gap, controlsRow.getWidth()));
    offlineButton.setBounds(controlsRow.removeFromRight(std::min(76, controlsRow.getWidth())));
    controlsRow.removeFromRight(std::min(ana::ui::gap, controlsRow.getWidth()));

    freezeButton.setBounds(controlsRow.removeFromRight(std::min(72, controlsRow.getWidth())));
    controlsRow.removeFromRight(std::min(ana::ui::gap, controlsRow.getWidth()));
    clearButton.setBounds(controlsRow.removeFromRight(std::min(60, controlsRow.getWidth())));
    controlsRow.removeFromRight(std::min(ana::ui::gap, controlsRow.getWidth()));
    realtimeButton.setBounds(controlsRow.removeFromRight(std::min(86, controlsRow.getWidth())));
    controlsRow.removeFromRight(std::min(ana::ui::gap, controlsRow.getWidth()));

    area.removeFromTop(std::min(ana::ui::gap, area.getHeight()));
    scopeDisplay.setBounds(area);

    const auto popupWidth = std::min(area.getWidth(),
        std::min(420, std::max(320, juce::roundToInt(static_cast<float>(area.getWidth()) * 0.42f))));
    crossoverSettings.setBounds(area.removeFromRight(popupWidth));

    if (showingCrossoverSettings)
        crossoverSettings.toFront(false);

    if (choicePrompt != nullptr)
    {
        choicePrompt->setBounds(getLocalBounds());
        choicePrompt->toFront(true);
    }
}
